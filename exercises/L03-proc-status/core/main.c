// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>

#include "rootkit.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 2
#error "unknown RKLINGS_FAIL_POINT"
#endif

static bool active = true;
module_param(active, bool, 0444);
MODULE_PARM_DESC(active, "Start active");

static struct rk_state *state;

static int __init rk_init(void)
{
	int error;

	state = rk_state_create(active);
	if (IS_ERR(state)) {
		error = PTR_ERR(state);
		state = NULL;
		return error;
	}

	// TODO 4: publish the proc entry or release the state

	return 0;
}

static void __exit rk_exit(void)
{
	// TODO 5: remove the proc entry before releasing the state
	rk_state_destroy(state);
	state = NULL;
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings proc status");
