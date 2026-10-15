// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include "rklings_hook.h"

#define RK_PREFIX "rklings."
#define RK_PREFIX_LEN 8

RK_ON_MKDIR(name) {
	/* TODO: If name starts with RK_PREFIX, log the suffix to dmesg.
	 * Always return -ENOENT. */
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
