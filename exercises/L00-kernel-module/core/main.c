// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init rk_init(void)
{
	// TODO 2: log "rklings: loaded" and return success
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO 3: log "rklings: unloaded"
}

// TODO 4: register both lifecycle callbacks

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings module lifecycle");
