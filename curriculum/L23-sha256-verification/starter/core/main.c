// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "selftest.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 1
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static int __init rootkitlings_init(void)
{
	unsigned int assertions;
	int error = rk_sha256_selftest(&assertions);

	if (error)
		return error;
	pr_info("rootkitlings: sha256 self-tests passed: %u assertions\n",
		assertions);
	return 0;
}

static void __exit rootkitlings_exit(void)
{
}

module_init(rootkitlings_init);
module_exit(rootkitlings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings SHA-256 verification");
