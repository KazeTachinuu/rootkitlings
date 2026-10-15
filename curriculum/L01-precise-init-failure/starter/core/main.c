// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_PREFLIGHT = 1,
};

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 1
#error "unknown RKLINGS_FAIL_POINT"
#endif

static int __init rk_init(void)
{
	/* TODO: if RKLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT, return -ECANCELED. */

	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
