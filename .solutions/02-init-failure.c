// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init hello_init(void)
{
	pr_info("hello: loaded\n");
	return -EINVAL;
}

static void __exit hello_exit(void)
{
	pr_info("hello: unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Init failure and automatic cleanup");
