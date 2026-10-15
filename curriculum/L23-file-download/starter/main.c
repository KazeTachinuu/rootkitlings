// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/uio.h>
#include <net/net_namespace.h>

#define RK_CHUNK_SIZE	4096

static unsigned short peer_port = 4444;
module_param(peer_port, ushort, 0);

static char peer_addr_str[16] = "127.0.0.1";
module_param_string(peer_address, peer_addr_str, sizeof(peer_addr_str), 0);

/* ------------------------------------------------------------------ */
/* Socket helpers                                                      */
/* ------------------------------------------------------------------ */

static struct socket *rk_connect(void)
{
	struct sockaddr_in peer = {
		.sin_family = AF_INET,
		.sin_port = htons(peer_port),
	};
	struct socket *sock;
	int error;

	peer.sin_addr.s_addr = in_aton(peer_addr_str);

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
				 IPPROTO_TCP, &sock);
	if (error)
		return ERR_PTR(error);

	error = kernel_connect(sock, (struct sockaddr_unsized *)&peer,
			       sizeof(peer), 0);
	if (error) {
		sock_release(sock);
		return ERR_PTR(error);
	}

	return sock;
}

/* Send all bytes in buf, handling partial sends. */
static int rk_send_all(struct socket *sock, const void *buf, size_t len)
{
	size_t offset = 0;

	while (offset < len) {
		struct msghdr msg = { };
		struct kvec vec = {
			.iov_base = (void *)buf + offset,
			.iov_len = len - offset,
		};
		ssize_t sent;

		sent = kernel_sendmsg(sock, &msg, &vec, 1, len - offset);
		if (sent < 0)
			return (int)sent;
		if (!sent)
			return -EPIPE;
		offset += sent;
	}

	return 0;
}

// Send a length-prefixed frame. Zero-length frame = EOF marker.

/* ------------------------------------------------------------------ */
/* File download: read and send in chunks                              */
/* ------------------------------------------------------------------ */

// Read a file in chunks, send each chunk as a frame, end with EOF frame.

/* ------------------------------------------------------------------ */
/* Demo entry point                                                    */
/* ------------------------------------------------------------------ */

// Connect, download the file, tear down the socket.

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static char *target_file;
module_param(target_file, charp, 0);

static int __init rk_init(void)
{
	// Download target_file if provided.
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
