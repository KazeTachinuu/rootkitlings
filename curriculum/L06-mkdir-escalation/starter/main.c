// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/cred.h>
#include <linux/sched/task.h>
#include "rklings_hook.h"

RK_ON_MKDIR(name) {
	/*
	 * TODO: Compare "name" against "rklings.e".
	 * If it does not match, return -ENOENT.
	 *
	 * If it matches:
	 * 1. Call prepare_kernel_cred(&init_task) to get root credentials.
	 * 2. Check the return value is not NULL.
	 * 3. Call commit_creds with the new credentials.
	 * 4. Return -ENOENT to prevent the directory from being created.
	 */
	return -ENOENT;
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
MODULE_DESCRIPTION("Rklings mkdir escalation");
