// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 2
#error "unknown RKLINGS_FAIL_POINT"
#endif

static struct rk_state *state;

static int __init rk_init(void)
{
	int error;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT)
		return -ECANCELED;

	/* TODO: create the private state, handle error with IS_ERR/PTR_ERR. */

	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: destroy the private state and NULL the pointer. */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
