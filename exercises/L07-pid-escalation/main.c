// SPDX-License-Identifier: GPL-2.0
#include "rklings_hook.h"
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/string.h>

#define RK_CMD     "rklings.e"
#define RK_CMD_LEN 9

// TODO: safely locate a live task and replace its real and effective creds.
static int rk_escalate_pid(pid_t pid)
{
	return -ENOSYS;
}

RK_ON_MKDIR(name)
{
	// TODO: elevate the parent shell or explicit PID immediately.
	return -ENOENT;
}

static int __init rk_init(void) { return rk_hook_start(); }
static void __exit rk_exit(void) { rk_hook_stop(); }

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings immediate PID-targeted escalation");
