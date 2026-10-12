// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 2
#error "unknown RKLINGS_FAIL_POINT"
#endif

static struct rk_netfilter_observer observer;

static int __init rk_init(void)
{
	int error;

	error = rk_netfilter_start(&observer);
	if (error)
		return error;

	error = rk_proc_publish(&observer);
	if (error)
		goto stop_netfilter;

	return 0;

stop_netfilter:
	rk_netfilter_stop(&observer);
	return error;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	rk_netfilter_stop(&observer);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local IPv4 Netfilter observer");
