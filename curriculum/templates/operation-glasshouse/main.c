// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/init.h>
#include <linux/module.h>

static int __init rootkit_init(void)
{
	/* TODO: acquire control, concealment, and persistence in order. */
	return -EOPNOTSUPP;
}

static void __exit rootkit_exit(void)
{
	/* TODO: reveal and release every acquired component in reverse order. */
}

module_init(rootkit_init);
module_exit(rootkit_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Operation Glasshouse");
