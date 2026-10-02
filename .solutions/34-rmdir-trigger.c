// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define PREFIX "/tmp/.rk-"

static unsigned long target_addr;
static atomic_t cmd_count = ATOMIC_INIT(0);

typedef long (*orig_rmdir_t)(struct pt_regs *);
static orig_rmdir_t orig_rmdir;

static unsigned long lookup_name(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long addr;

	if (register_kprobe(&kp) < 0)
		return 0;
	addr = (unsigned long)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

static void dispatch(const char *cmd)
{
	if (strcmp(cmd, "ping") == 0) {
		pr_info("pong\n");
	} else if (strcmp(cmd, "id") == 0) {
		pr_info("uid=%u\n", from_kuid(&init_user_ns, current_uid()));
	} else if (strcmp(cmd, "count") == 0) {
		pr_info("count=%d\n", atomic_inc_return(&cmd_count));
	}
}

static void notrace hook_rmdir(unsigned long ip, unsigned long parent_ip,
			       struct ftrace_ops *ops,
			       struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	const char __user *upath;
	char kpath[128];
	long len;

	upath = (const char __user *)regs->di;
	len = strncpy_from_user(kpath, upath, sizeof(kpath));
	if (len <= 0) {
		orig_rmdir(regs);
		return;
	}

	if (strncmp(kpath, PREFIX, sizeof(PREFIX) - 1) == 0) {
		dispatch(kpath + sizeof(PREFIX) - 1);
		regs_set_return_value(regs, 0);
		return;
	}

	orig_rmdir(regs);
}

static struct ftrace_ops hook_ops = {
	.func  = hook_rmdir,
	.flags = FTRACE_OPS_FL_SAVE_REGS
	       | FTRACE_OPS_FL_IPMODIFY
	       | FTRACE_OPS_FL_RECURSION,
};

static int install_hook(void)
{
	int err;

	target_addr = lookup_name("__x64_sys_rmdir");
	if (!target_addr)
		return -ENOENT;
	orig_rmdir = (orig_rmdir_t)target_addr;

	err = ftrace_set_filter_ip(&hook_ops, target_addr, 0, 0);
	if (err)
		return err;

	err = register_ftrace_function(&hook_ops);
	if (err) {
		ftrace_set_filter_ip(&hook_ops, target_addr, 1, 0);
		return err;
	}
	return 0;
}

static void remove_hook(void)
{
	unregister_ftrace_function(&hook_ops);
	ftrace_set_filter_ip(&hook_ops, target_addr, 1, 0);
}

static int __init rmdir_trigger_init(void)
{
	int err;

	err = install_hook();
	if (err)
		return err;

	pr_info("rk: rmdir trigger installed\n");
	return 0;
}

static void __exit rmdir_trigger_exit(void)
{
	remove_hook();
	pr_info("rk: rmdir trigger removed\n");
}

module_init(rmdir_trigger_init);
module_exit(rmdir_trigger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rmdir-based command trigger");
