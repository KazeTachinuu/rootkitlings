// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

#include "rootkit.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 3
#error "unknown RKLINGS_FAIL_POINT"
#endif

static struct rk_state *state;

static int __init rk_init(void)
{
	int error;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT)
		return -ECANCELED;

	state = rk_state_create();
	if (IS_ERR(state)) {
		error = PTR_ERR(state);
		state = NULL;
		return error;
	}

	error = rk_proc_publish();
	if (error)
		goto destroy_state;

	pr_info("rklings: loaded\n");
	return 0;

destroy_state:
	rk_state_destroy(state);
	state = NULL;
	return error;
}

static void __exit rk_exit(void)
{
	pr_info("rklings: unloaded\n");
	rk_proc_unpublish();
	rk_state_destroy(state);
	state = NULL;
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings kernel module");
