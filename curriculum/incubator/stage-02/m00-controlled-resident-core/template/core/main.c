// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 4
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static struct rk_state *state;

static int __init rk_init(void)
{
	int error;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT)
		return -ECANCELED;

	state = rk_state_create();
	if (IS_ERR(state)) {
		error = PTR_ERR(state);
		state = NULL;
		return error;
	}

	/* TODO: start the worker before publishing either request source. */
	error = rk_proc_publish(state);
	if (error)
		goto destroy_state;

	/* TODO: arm the timer only after procfs publication succeeds. */
	return 0;

destroy_state:
	rk_state_destroy(state);
	state = NULL;
	return error;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	/* TODO: stop the timer, then stop and join the worker. */
	rk_state_destroy(state);
	state = NULL;
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings kernel module");
