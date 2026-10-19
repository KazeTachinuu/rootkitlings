// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 1
#error "unknown RKLINGS_FAIL_POINT"
#endif

static int __init rk_init(void)
{
	if (RKLINGS_FAIL_POINT == 1)
		return -EINVAL;
	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
