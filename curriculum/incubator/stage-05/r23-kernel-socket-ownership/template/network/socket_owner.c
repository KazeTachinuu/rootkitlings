// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <net/net_namespace.h>

#include "rootkit.h"

__must_check int rk_socket_open(struct rk_socket_owner *owner,
				const struct rk_socket_config *config)
{
	/* Create, connect, validate both endpoints, then publish ownership. */
	(void)owner;
	(void)config;
	return -EOPNOTSUPP;
}

void rk_socket_close(struct rk_socket_owner *owner)
{
	/* Invalidate ownership before shutdown and unconditional release. */
	(void)owner;
}

void rk_socket_snapshot(struct rk_socket_owner *owner,
			struct rk_socket_status *status)
{
	/* Copy one consistent view of the owned socket evidence. */
	(void)owner;
	memset(status, 0, sizeof(*status));
}
