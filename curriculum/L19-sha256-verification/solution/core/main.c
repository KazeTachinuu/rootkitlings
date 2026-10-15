// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "selftest.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 1
#error "unknown RKLINGS_FAIL_POINT"
#endif

static int __init rklings_init(void)
{
	unsigned int assertions;
	int error = rk_sha256_selftest(&assertions);

	if (error)
		return error;
	pr_info("rklings: sha256 self-tests passed: %u assertions\n",
		assertions);
	return 0;
}

static void __exit rklings_exit(void)
{
}

module_init(rklings_init);
module_exit(rklings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings SHA-256 verification");
