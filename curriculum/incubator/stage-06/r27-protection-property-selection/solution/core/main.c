// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include "selftest.h"

static int __init rootkitlings_init(void)
{
	char runtime_marker[64];
	unsigned int assertions;
	int error;

	error = rk_protection_selftest(&assertions, runtime_marker,
				       sizeof(runtime_marker));
	if (error)
		return error;
	pr_info("rootkitlings: protection self-tests passed: %u assertions\n",
		assertions);
	pr_info("rootkitlings: runtime marker=%s\n", runtime_marker);
	memzero_explicit(runtime_marker, sizeof(runtime_marker));
	return 0;
}

static void __exit rootkitlings_exit(void)
{
}

module_init(rootkitlings_init);
module_exit(rootkitlings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings protection-property selection");
