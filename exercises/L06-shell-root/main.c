// SPDX-License-Identifier: GPL-2.0
#include "rklings_creds.h"
#include "rklings_hook.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/task.h>

RK_ON_MKDIR()
{
	// TODO 1: elevate the parent shell for the exact command
	return 0;
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
MODULE_DESCRIPTION("Rklings calling-shell escalation");
