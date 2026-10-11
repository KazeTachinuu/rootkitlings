// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 5
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static int __init rk_init(void)
{
	int error;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PREFLIGHT)
		return -ECANCELED;

	error = rk_read_target_retain();
	if (error)
		return error;

	error = rk_read_filter_start();
	if (error)
		goto release_target;

	error = rk_proc_publish();
	if (error)
		goto stop_filter;

	return 0;

stop_filter:
	rk_read_filter_stop();
release_target:
	rk_read_target_release();
	return error;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	rk_read_filter_stop();
	rk_read_target_release();
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings kernel module");
