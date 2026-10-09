// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 2
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);
MODULE_PARM_DESC(peer_port, "loopback TCP listener port");

static struct rk_session session;

static int __init rk_init(void)
{
	int error;

	if (!peer_port)
		return -EINVAL;
	error = rk_run_selftests();
	if (error)
		return error;
	error = rk_session_start(&session, peer_port);
	if (error)
		return error;
	error = rk_proc_publish(&session);
	if (error) {
		rk_session_stop(&session);
		return error;
	}
	return 0;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	rk_session_stop(&session);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings cancellable framed TCP session");
