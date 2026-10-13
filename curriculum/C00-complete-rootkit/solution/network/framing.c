// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/minmax.h>
#include <linux/net.h>
#include <linux/uio.h>

#include "framing.h"

static int rk_send_exact(struct socket *socket, const unsigned char *bytes,
			 size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		struct msghdr msg = { };
		struct kvec vec = {
			.iov_base = (void *)(bytes + offset),
			.iov_len = length - offset,
		};
		ssize_t sent;

		sent = kernel_sendmsg(socket, &msg, &vec, 1, length - offset);
		if (sent < 0)
			return sent;
		if (sent == 0)
			return -EPIPE;
		offset += sent;
	}
	return 0;
}

static int rk_receive_exact(struct socket *socket, unsigned char *bytes,
			    size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		struct msghdr msg = { };
		struct kvec vec = {
			.iov_base = bytes + offset,
			.iov_len = length - offset,
		};
		ssize_t got;

		got = kernel_recvmsg(socket, &msg, &vec, 1, length - offset, 0);
		if (got < 0)
			return got;
		if (got == 0)
			return -ECONNRESET;
		offset += got;
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
	error = rk_send_exact(socket, header, sizeof(header));
	if (error)
		return error;
	return rk_send_exact(socket, payload, length);
}

int rk_receive_frame(struct socket *socket, struct rk_frame *frame)
{
	unsigned char header[2];
	int error;

	if (!socket || !frame)
		return -EINVAL;
	error = rk_receive_exact(socket, header, sizeof(header));
	if (error)
		return error;
	frame->length = ((size_t)header[0] << 8) | header[1];
	if (frame->length > RK_FRAME_MAX) {
		unsigned char scratch[64];
		size_t remaining = frame->length;

		while (remaining) {
			size_t chunk = min_t(size_t, remaining, sizeof(scratch));

			error = rk_receive_exact(socket, scratch, chunk);
			if (error)
				return error;
			remaining -= chunk;
		}
		frame->length = 0;
		return -EMSGSIZE;
	}
	error = rk_receive_exact(socket, frame->payload, frame->length);
	if (error) {
		frame->length = 0;
		return error;
	}
	return 0;
}
