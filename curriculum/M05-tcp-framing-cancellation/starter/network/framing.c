// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>

#include "framing.h"

int rk_send_frame(const struct rk_transport_ops *ops, void *context,
		  struct socket *socket, const unsigned char *payload,
		  size_t length)
{
	(void)ops;
	(void)context;
	(void)socket;
	(void)payload;
	(void)length;
	return -EOPNOTSUPP;
}

int rk_receive_frame(const struct rk_transport_ops *ops, void *context,
		     struct socket *socket, struct rk_frame *frame)
{
	(void)ops;
	(void)context;
	(void)socket;
	(void)frame;
	return -EOPNOTSUPP;
}
