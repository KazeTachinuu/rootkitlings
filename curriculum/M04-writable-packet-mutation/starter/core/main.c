// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "rootkit.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 2
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static ushort match_port = 47021;
module_param(match_port, ushort, 0400);
MODULE_PARM_DESC(match_port, "Destination UDP port to match");

static ushort replacement_port = 47022;
module_param(replacement_port, ushort, 0400);
MODULE_PARM_DESC(replacement_port, "Destination UDP port to write");

static struct rk_netfilter_mutator mutator;

static int __init rk_init(void)
{
	const struct rk_netfilter_config config = {
		.match_port = match_port,
		.replacement_port = replacement_port,
		.registration_error =
			ROOTKITLINGS_FAIL_POINT == RK_FAIL_NETFILTER_REGISTER ?
			-EAFNOSUPPORT : 0,
	};
	int error;

	if (!match_port || !replacement_port || match_port == replacement_port)
		return -EINVAL;
	error = rk_run_selftests();
	if (error)
		return error;

	error = rk_netfilter_start(&mutator, &config);
	if (error)
		return error;

	error = rk_proc_publish(&mutator);
	if (error)
		goto stop_netfilter;

	return 0;

stop_netfilter:
	rk_netfilter_stop(&mutator);
	return error;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	rk_netfilter_stop(&mutator);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings bounded UDP destination rewrite");
