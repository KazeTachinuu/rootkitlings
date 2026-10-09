// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

static int __init rk_init(void)
{
	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings kernel module");
