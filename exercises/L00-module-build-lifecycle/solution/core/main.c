// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init rk_init(void)
{
	pr_info("rklings: loaded\n");
	return 0;
}

static void __exit rk_exit(void)
{
	pr_info("rklings: unloaded\n");
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
