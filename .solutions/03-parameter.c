// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>

static char *name = "world";

module_param(name, charp, 0644);
MODULE_PARM_DESC(name, "Target identifier for the implant");

static int __init param_init(void)
{
	pr_info("rk: loaded name=%s\n", name);
	return 0;
}

static void __exit param_exit(void)
{
	pr_info("rk: unloaded name=%s\n", name);
}

module_init(param_init);
module_exit(param_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module parameter passing");
