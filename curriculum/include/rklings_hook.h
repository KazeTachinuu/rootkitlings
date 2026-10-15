/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RKLINGS_HOOK_H
#define _RKLINGS_HOOK_H
/*
 * Black-box mkdir interception header for rootkitlings L05-L07.
 *
 * Students include this header without reading it. They define a handler
 * using the RK_ON_MKDIR(name) macro and call rk_hook_start/rk_hook_stop
 * from module_init/module_exit. The header does the rest.
 *
 * L08 opens this file. L09 discards it and writes kretprobe from scratch.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define RK_HOOK_PATH_MAX 128

struct __rk_mkdir_data {
	int result;
	bool intercepted;
};

/*
 * Student defines their handler with this macro:
 *   RK_ON_MKDIR(name) {
 *       if (strcmp(name, "rklings.hello") == 0)
 *           pr_info("intercepted: hello\n");
 *       return -ENOENT;
 *   }
 *
 * name: the basename of the directory the user tried to create.
 * Return 0 for success, negative errno for failure.
 */
#define RK_ON_MKDIR(param_name) \
	static int __rk_user_handler(const char *param_name)

static int __rk_entry(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct __rk_mkdir_data *data = (void *)inst->data;
	struct pt_regs *uregs;
	const char __user *pathname;
	char buf[RK_HOOK_PATH_MAX];
	long copied;
	const char *base;

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

	data->result = __rk_user_handler(base);
	data->intercepted = true;

	put_user('\0', (char __user *)pathname);
	return 0;
}

static int __rk_return(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct __rk_mkdir_data *data = (void *)inst->data;

	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe __rk_probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler  = __rk_entry,
	.handler        = __rk_return,
	.data_size      = sizeof(struct __rk_mkdir_data),
	.maxactive      = 8,
};

static inline int rk_hook_start(void)
{
	return register_kretprobe(&__rk_probe);
}

static inline void rk_hook_stop(void)
{
	unregister_kretprobe(&__rk_probe);
}

#endif /* _RKLINGS_HOOK_H */
