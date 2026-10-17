// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init rk_init(void)
{
	// TODO: log "rklings: loaded" and return success
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: log "rklings: unloaded"
}

// TODO: register both callbacks and declare GPL license + description
