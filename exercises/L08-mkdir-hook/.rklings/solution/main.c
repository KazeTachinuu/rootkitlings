// SPDX-License-Identifier: GPL-2.0
#include "rklings_hook.h"
#include <linux/init.h>
#include <linux/module.h>

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
MODULE_DESCRIPTION("Rklings mkdir hook");
