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

/* Send exactly len bytes, retrying on short writes */
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

/* Receive exactly len bytes, retrying on short reads */
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

/* Send a length-prefixed frame: 2-byte big-endian length, then payload */
static int send_frame(struct socket *sock, const void *buf, size_t len)
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

/* Receive a length-prefixed frame into buf; writes payload length to out_len */
static int recv_frame(struct socket *sock, void *buf, size_t max_len,
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

	error = kernel_connect(sock, (struct sockaddr *)&addr,
			       sizeof(addr), 0);
	if (error)
		goto out;

	/* Send framed PING */
	error = send_frame(sock, payload, 4);
	if (error < 0)
		goto out;

	/* Receive framed response */
	error = recv_frame(sock, rxbuf, sizeof(rxbuf), &rxlen);

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
MODULE_DESCRIPTION("Length-prefix message framing over TCP");
