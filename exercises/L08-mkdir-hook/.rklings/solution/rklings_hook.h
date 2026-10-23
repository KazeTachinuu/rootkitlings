/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RKLINGS_HOOK_H
#define _RKLINGS_HOOK_H

#include "rklings_command.h"
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

struct rk_call {
	int result;
	bool intercepted;
};

static int rk_do_intercept(struct rk_call *call,
			   const char __user *pathname)
{
	char buf[RK_PATH_MAX];
	long copied;

	call->intercepted = false;
	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0 || copied == sizeof(buf))
		return 0;

	if (strncmp(buf, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;
	if (put_user('\0', (char __user *)pathname))
		return 0;

	call->intercepted = true;
	call->result = rk_handle_mkdir(buf);
	return 0;
}

static int rk_entry_mkdir(struct kretprobe_instance *instance,
			  struct pt_regs *regs)
{
	struct pt_regs *syscall_regs;

	syscall_regs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!syscall_regs)
		return 0;
	return rk_do_intercept((void *)instance->data,
			       (const char __user *)syscall_regs->di);
}

static int rk_entry_mkdirat(struct kretprobe_instance *instance,
			    struct pt_regs *regs)
{
	struct pt_regs *syscall_regs;

	syscall_regs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!syscall_regs)
		return 0;
	return rk_do_intercept((void *)instance->data,
			       (const char __user *)syscall_regs->si);
}

static int rk_return(struct kretprobe_instance *instance, struct pt_regs *regs)
{
	struct rk_call *call = (void *)instance->data;

	if (call->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)call->result);
	return 0;
}

static struct kretprobe rk_mkdir_probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler = rk_entry_mkdir,
	.handler = rk_return,
	.data_size = sizeof(struct rk_call),
	.maxactive = 8,
};

static struct kretprobe rk_mkdirat_probe = {
	.kp.symbol_name = RK_MKDIRAT_SYMBOL,
	.entry_handler = rk_entry_mkdirat,
	.handler = rk_return,
	.data_size = sizeof(struct rk_call),
	.maxactive = 8,
};

static int rk_hook_start(void)
{
	int ret;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		return ret;
	ret = register_kretprobe(&rk_mkdirat_probe);
	if (ret) {
		unregister_kretprobe(&rk_mkdir_probe);
		return ret;
	}
	return 0;
}

static void rk_hook_stop(void)
{
	unregister_kretprobe(&rk_mkdirat_probe);
	unregister_kretprobe(&rk_mkdir_probe);
}

#endif /* _RKLINGS_HOOK_H */
