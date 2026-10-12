// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/minmax.h>
#include <linux/net.h>
#include <linux/uio.h>

#include "framing.h"

static int send_exact(struct socket *socket, const unsigned char *bytes,
		      size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		struct msghdr message = { };
		struct kvec vector = {
			.iov_base = (void *)(bytes + offset),
			.iov_len = length - offset,
		};
		ssize_t result;

		result = kernel_sendmsg(socket, &message, &vector, 1,
					length - offset);
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

static int receive_exact(struct socket *socket, unsigned char *bytes,
			 size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		struct msghdr message = { };
		struct kvec vector = {
			.iov_base = bytes + offset,
			.iov_len = length - offset,
		};
		ssize_t result;

		result = kernel_recvmsg(socket, &message, &vector, 1,
					length - offset, 0);
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

int rk_send_frame(struct socket *socket, const unsigned char *payload,
		  size_t length)
{
	unsigned char header[2];
	int error;

	if (!socket || length > RK_FRAME_MAX || (length && !payload))
		return -EINVAL;
	header[0] = length >> 8;
	header[1] = length;
	error = send_exact(socket, header, sizeof(header));
	if (error)
		return error;
	return send_exact(socket, payload, length);
}

int rk_receive_frame(struct socket *socket, struct rk_frame *frame)
{
	unsigned char scratch[RK_DRAIN_CHUNK];
	unsigned char header[2];
	struct rk_frame received = { };
	size_t remaining;
	int error;

	if (!socket || !frame)
		return -EINVAL;
	error = receive_exact(socket, header, sizeof(header));
	if (error)
		return error;
	received.length = ((size_t)header[0] << 8) | header[1];
	if (received.length <= RK_FRAME_MAX) {
		error = receive_exact(socket, received.payload,
				      received.length);
		if (error)
			return error;
		*frame = received;
		return 0;
	}

	remaining = received.length;
	while (remaining) {
		size_t chunk = min_t(size_t, remaining, sizeof(scratch));

		error = receive_exact(socket, scratch, chunk);
		if (error)
			return error;
		remaining -= chunk;
	}
	return -EMSGSIZE;
}
