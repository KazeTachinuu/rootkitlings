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

/*
 * TODO: Implement rk_send_frame.
 *
 * Send a length-prefixed frame: a 4-byte big-endian length header
 * followed by the data. Use htonl to convert the length. If len is 0,
 * send only the header (this is the EOF marker).
 */

/* ------------------------------------------------------------------ */
/* File download: read and send in chunks                              */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_download_file(struct socket *sock,
 *                                   const char *filepath).
 *
 * 1. Open the file with filp_open(filepath, O_RDONLY, 0).
 *    If it fails (IS_ERR), send an error frame containing the error
 *    code as a big-endian 4-byte value, then return the error.
 *
 * 2. Allocate a RK_CHUNK_SIZE buffer with kmalloc(RK_CHUNK_SIZE,
 *    GFP_KERNEL). If allocation fails, close the file and return
 *    -ENOMEM.
 *
 * 3. Loop:
 *    a. kernel_read(filp, buf, RK_CHUNK_SIZE, &pos) into the buffer.
 *    b. If bytes_read < 0, store the error and break.
 *    c. If bytes_read == 0, EOF reached, break.
 *    d. Send the chunk as a framed message with rk_send_frame.
 *
 * 4. After the loop, if no error, send a zero-length frame (EOF).
 *
 * 5. kfree the buffer and filp_close the file.
 */

/* ------------------------------------------------------------------ */
/* Demo entry point                                                    */
/* ------------------------------------------------------------------ */

/*
 * TODO: Implement rk_do_download(const char *path).
 *
 * 1. Connect with rk_connect().
 * 2. Call rk_download_file.
 * 3. kernel_sock_shutdown + sock_release.
 * 4. Return the error from rk_download_file.
 */

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static char *target_file;
module_param(target_file, charp, 0);

static int __init rk_init(void)
{
	/*
	 * TODO: If target_file is non-NULL and non-empty,
	 * call rk_do_download(target_file) and return its result.
	 */
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
