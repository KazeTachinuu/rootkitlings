// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "selftest.h"

static int __init rklings_init(void)
{
	unsigned int assertions;
	int error;

	error = rk_authenticated_selftest(&assertions);
	if (error)
		return error;
	pr_info("rklings: authenticated replay-safe message passed: %u "
		"assertions\n",
		assertions);
	return 0;
}

static void __exit rklings_exit(void)
{
}

module_init(rklings_init);
module_exit(rklings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings authenticated replay-safe message");
