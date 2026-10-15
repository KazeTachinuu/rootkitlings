// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include "rklings_hook.h"

#define RK_PREFIX "rklings."
#define RK_PREFIX_LEN 8

RK_ON_MKDIR(name) {
	/*
	 * TODO: Check if "name" starts with RK_PREFIX.
	 * If it does not match, return -ENOENT.
	 *
	 * If it matches, print the part after the prefix to dmesg
	 * using pr_info, then return -ENOENT.
	 */
}

static int __init rk_init(void)
{
	/* TODO: Start the hook and return its result. */
	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: Stop the hook. */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings mkdir reaction");
