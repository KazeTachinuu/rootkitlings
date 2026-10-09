// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 2
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static struct rk_state *state;

static int __init rk_init(void)
{
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT)
		return -ECANCELED;

	state = rk_state_create();
	if (IS_ERR(state)) {
		state = NULL;
		return -ENOMEM;
	}
	return 0;
}

static void __exit rk_exit(void)
{
	rk_state_destroy(state);
	state = NULL;
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings kernel module");
