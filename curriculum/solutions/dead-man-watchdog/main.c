// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "watchdog.h"

static int __init watchdog_module_init(void)
{
	return watchdog_start();
}

static void __exit watchdog_module_exit(void)
{
	watchdog_stop();
}

module_init(watchdog_module_init);
module_exit(watchdog_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dead-man heartbeat watchdog");
