// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "selftest.h"

static int __init rootkitlings_init(void)
{
	unsigned int assertions;
	int error;

	error = rk_dns_name_selftest(&assertions);
	if (error)
		return error;
	pr_info("rootkitlings: dns name codec self-tests passed: %u assertions\n",
		assertions);
	return 0;
}

static void __exit rootkitlings_exit(void)
{
}

module_init(rootkitlings_init);
module_exit(rootkitlings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings bounded DNS wire-name codec");
