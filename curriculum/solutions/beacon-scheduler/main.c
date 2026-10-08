// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "beacon.h"
#include "status.h"

static int __init beacon_module_init(void)
{
	int error;

	error = beacon_start();
	if (error)
		return error;

	error = status_create();
	if (error) {
		beacon_stop();
		return error;
	}

	return 0;
}

static void __exit beacon_module_exit(void)
{
	status_remove();
	beacon_stop();
}

module_init(beacon_module_init);
module_exit(beacon_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Periodic kernel beacon");
