// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include "selftest.h"

static int __init rklings_init(void)
{
	char runtime_marker[64];
	unsigned int assertions;
	int error;

	error = rk_protection_selftest(&assertions, runtime_marker,
				       sizeof(runtime_marker));
	if (error)
		return error;
	pr_info("rklings: protection self-tests passed: %u assertions\n",
		assertions);
	pr_info("rklings: runtime marker=%s\n", runtime_marker);
	memzero_explicit(runtime_marker, sizeof(runtime_marker));
	return 0;
}

static void __exit rklings_exit(void)
{
}

module_init(rklings_init);
module_exit(rklings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings protection-property selection");
