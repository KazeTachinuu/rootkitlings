// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/net_namespace.h>

#include "rootkit.h"

#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 3
#error "unknown RKLINGS_FAIL_POINT"
#endif

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);
MODULE_PARM_DESC(peer_port, "loopback TCP listener port");

static struct rk_socket_owner owner = {
	.lock = __MUTEX_INITIALIZER(owner.lock),
};

static int __init rk_init(void)
{
	const struct rk_socket_config config = {
		.net = &init_net,
		.family = AF_INET,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_TCP,
		.connect_flags = 0,
		.peer = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
			.sin_port = htons(peer_port),
		},
	};
	int error;

	error = rk_socket_open(&owner, &config);
	if (error)
		return error;

	error = rk_proc_publish(&owner);
	if (error) {
		rk_socket_close(&owner);
		return error;
	}

	return 0;
}

static void __exit rk_exit(void)
{
	rk_proc_unpublish();
	rk_socket_close(&owner);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings owned loopback TCP socket");
