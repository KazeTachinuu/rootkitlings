// SPDX-License-Identifier: GPL-2.0
#include "rklings_hook.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8

RK_ON_MKDIR(name)
{
	// TODO: log intercepted suffix to dmesg (strncmp + pr_info)
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
MODULE_DESCRIPTION("Rklings mkdir reaction");
