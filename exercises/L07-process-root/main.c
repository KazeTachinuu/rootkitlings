// SPDX-License-Identifier: GPL-2.0
#include "rklings_hook.h"
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>

// TODO 1: replace one live userspace task's credentials
static int rk_escalate_pid(pid_t pid)
{
	return -ENOSYS;
}

static int __init rk_init(void)
{
	return rk_hook_start();
}

static void __exit rk_exit(void)
{
	rk_hook_stop();
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings process escalation");
