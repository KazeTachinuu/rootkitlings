// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/printk.h>

static int __init placeholder_init(void)
{
	pr_info("rk: replace placeholder.o with main.o and calc.o\n");
	return 0;
}

static void __exit placeholder_exit(void)
{
}

module_init(placeholder_init);
module_exit(placeholder_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Multi-file Kbuild placeholder");
