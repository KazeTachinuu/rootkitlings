// SPDX-License-Identifier: GPL-2.0
//
// Length-prefixed framing over TCP (2-byte big-endian header).
//
// Every message on the wire is: [__be16 length][payload].
// This is the transport layer under the AEAD envelope.
//
#include <linux/net.h>
#include <linux/types.h>

#include "rootkit.h"

static int send_exact(struct socket *sock, const void *buf, size_t len)
{
	// TODO: Loop kernel_sendmsg until all bytes sent.
	// Return total sent, or negative error.
	// Return -ECONNRESET if sendmsg returns 0.
	return -ENOSYS;
}

static int recv_exact(struct socket *sock, void *buf, size_t len)
{
	// TODO: Loop kernel_recvmsg until all bytes received.
	// Return total received, or negative error.
	// Return -ECONNRESET if recvmsg returns 0.
	return -ENOSYS;
}

int rk_send_frame(struct socket *sock, const void *buf, size_t len)
{
	// TODO:
	// 1. Check len <= RK_FRAME_MAX
	// 2. hdr = htons((u16)len)
	// 3. send_exact the 2-byte header
	// 4. send_exact the payload (if len > 0)
	return -ENOSYS;
}

int rk_recv_frame(struct socket *sock, void *buf, size_t max_len,
		  size_t *out_len)
{
	// TODO:
	// 1. recv_exact 2-byte header
	// 2. payload_len = ntohs(hdr)
	// 3. Check payload_len <= max_len
	// 4. recv_exact the payload (if payload_len > 0)
	// 5. *out_len = payload_len
	return -ENOSYS;
}
