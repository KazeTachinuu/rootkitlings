// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/init.h>
#include <linux/module.h>

static int __init rk_init(void)
{
	// Initialize target lists, create proc entry, register all six
	// kretprobes. Use goto rollback: each failure unregisters the previous
	// probes.
	return 0;
}

static void __exit rk_exit(void)
{
	// Uncloak, unregister all probes in reverse order, remove proc entry.
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local control milestone");
