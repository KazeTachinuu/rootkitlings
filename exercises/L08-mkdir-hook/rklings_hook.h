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

// TODO 1: intercept one pathname and save the command result
static int rk_do_intercept(struct rk_call *call,
			   const char __user *pathname)
{
	return 0;
}

// TODO 2: extract the pathname from each syscall wrapper
static int rk_entry_mkdir(struct kretprobe_instance *instance,
			  struct pt_regs *regs)
{
	return 0;
}

static int rk_entry_mkdirat(struct kretprobe_instance *instance,
			    struct pt_regs *regs)
{
	return 0;
}

// TODO 3: return the saved command result
static int rk_return(struct kretprobe_instance *instance, struct pt_regs *regs)
{
	return 0;
}

// TODO 4: declare both kretprobes

static int rk_hook_start(void)
{
	// TODO 5: register both probes or roll back
	return 0;
}

static void rk_hook_stop(void)
{
	// TODO 6: unregister both probes in reverse order
}

#endif /* _RKLINGS_HOOK_H */
