// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/printk.h>
#include "calc.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multi-file module exercise");

static int __init rk_init(void)
{
	pr_info("rk: add=%d mul=%d\n", rk_add(3, 4), rk_mul(3, 4));
	return 0;
}

static void __exit rk_exit(void)
{
	pr_info("rk: multi-file module unloaded\n");
}

module_init(rk_init);
module_exit(rk_exit);
