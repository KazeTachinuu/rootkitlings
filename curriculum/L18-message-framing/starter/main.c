// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <net/net_namespace.h>

#define RK_FRAME_MAX	4096
#define RK_PORT		4444

/*
 * TODO: Implement send_exact.
 *
 * Send exactly len bytes from buf over sock, retrying on short writes.
 * kernel_sendmsg may send fewer bytes than requested on a TCP socket.
 * Loop until all bytes are sent. Return total bytes sent on success,
 * or a negative error. Return -ECONNRESET if kernel_sendmsg returns 0.
 */

/*
 * TODO: Implement recv_exact.
 *
 * Receive exactly len bytes into buf from sock, retrying on short reads.
 * kernel_recvmsg may return fewer bytes than requested on TCP.
 * Loop until all bytes are received. Return total bytes received on
 * success, or a negative error. Return -ECONNRESET on connection close.
 */

/*
 * TODO: Implement send_frame.
 *
 * Send a length-prefixed frame over the socket.
 * 1. Reject if len > RK_FRAME_MAX.
 * 2. Convert len to a 2-byte big-endian value with htons.
 * 3. Send the 2-byte header with send_exact.
 * 4. If len > 0, send the payload with send_exact.
 * 5. Zero-length frames are valid (just the 2-byte header).
 */

/*
 * TODO: Implement recv_frame.
 *
 * Receive a length-prefixed frame from the socket.
 * 1. Read exactly 2 bytes (the length header) with recv_exact.
 * 2. Convert from network byte order with ntohs.
 * 3. Reject if payload_len > max_len.
 * 4. If payload_len == 0, set *out_len = 0 and return 0.
 * 5. Read exactly payload_len bytes into buf with recv_exact.
 * 6. Set *out_len = payload_len and return 0.
 *
 * int recv_frame(struct socket *sock, void *buf, size_t max_len,
 *                size_t *out_len)
 */

static int __init rk_init(void)
{
	struct socket *sock = NULL;
	struct sockaddr_in addr;
	char payload[] = "PING";
	char rxbuf[64];
	size_t rxlen;
	int error;

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
				 IPPROTO_TCP, &sock);
	if (error)
		return error;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(RK_PORT);

	error = kernel_connect(sock, (struct sockaddr_unsized *)&addr,
			       sizeof(addr), 0);
	if (error)
		goto out;

	/* TODO: Send framed PING with send_frame */
	error = -ENOSYS;
	if (error < 0)
		goto out;

	/* TODO: Receive framed response with recv_frame */
	error = -ENOSYS;

out:
	kernel_sock_shutdown(sock, SHUT_RDWR);
	sock_release(sock);
	return error;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
