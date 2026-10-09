// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

static int __init rootkitlings_init(void)
{
	return 0;
}

static void __exit rootkitlings_exit(void)
{
}

module_init(rootkitlings_init);
module_exit(rootkitlings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings bounded IPv4 parser");
