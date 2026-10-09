// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/minmax.h>

#include "framing.h"

static int rk_send_exact(const struct rk_transport_ops *ops, void *context,
			 struct socket *socket, const unsigned char *bytes,
			 size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t result = ops->send(socket, bytes + offset,
					   length - offset, context);

		if (result < 0)
			return result;
		if (result == 0)
			return -EPIPE;
		if ((size_t)result > length - offset)
			return -EPROTO;
		offset += result;
	}
	return 0;
}

static int rk_receive_exact(const struct rk_transport_ops *ops, void *context,
			    struct socket *socket, unsigned char *bytes,
			    size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t result = ops->receive(socket, bytes + offset,
					      length - offset, context);

		if (result < 0)
			return result;
		if (result == 0)
			return -ECONNRESET;
		if ((size_t)result > length - offset)
			return -EPROTO;
		offset += result;
	}
	return 0;
}

int rk_send_frame(const struct rk_transport_ops *ops, void *context,
		  struct socket *socket, const unsigned char *payload,
		  size_t length)
{
	unsigned char header[2];
	int error;

	if (!ops || !ops->send || !socket || length > RK_FRAME_MAX ||
	    (length && !payload))
		return -EINVAL;
	header[0] = length >> 8;
	header[1] = length;
	error = rk_send_exact(ops, context, socket, header, sizeof(header));
	if (error)
		return error;
	return rk_send_exact(ops, context, socket, payload, length);
}

int rk_receive_frame(const struct rk_transport_ops *ops, void *context,
		     struct socket *socket, struct rk_frame *frame)
{
	unsigned char scratch[RK_DRAIN_CHUNK];
	unsigned char header[2];
	struct rk_frame received = { };
	size_t remaining;
	int error;

	if (!ops || !ops->receive || !socket || !frame)
		return -EINVAL;
	error = rk_receive_exact(ops, context, socket, header, sizeof(header));
	if (error)
		return error;
	received.length = ((size_t)header[0] << 8) | header[1];
	if (received.length <= RK_FRAME_MAX) {
		error = rk_receive_exact(ops, context, socket, received.payload,
					 received.length);
		if (error)
			return error;
		*frame = received;
		return 0;
	}

	remaining = received.length;
	while (remaining) {
		size_t chunk = min_t(size_t, remaining, sizeof(scratch));

		error = rk_receive_exact(ops, context, socket, scratch, chunk);
		if (error)
			return error;
		remaining -= chunk;
	}
	return -EMSGSIZE;
}
