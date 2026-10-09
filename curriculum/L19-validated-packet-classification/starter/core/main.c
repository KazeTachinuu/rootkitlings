// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "rootkit.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 2
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static ushort target_port = 47021;
module_param(target_port, ushort, 0400);
MODULE_PARM_DESC(target_port, "Destination UDP port to classify");

static struct rk_netfilter_observer observer;

static int __init rk_init(void)
{
	int error;

	if (!target_port)
		return -EINVAL;
	error = rk_run_selftests();
	if (error)
		return error;

	error = rk_netfilter_start(&observer, target_port);
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
MODULE_DESCRIPTION("Rootkitlings bounded IPv4 UDP classifier");
