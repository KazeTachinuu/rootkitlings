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

// Implement send_exact: retry short writes until all bytes sent.

// Implement recv_exact: retry short reads until all bytes received.

// Implement send_frame: send a 2-byte big-endian length header, then payload.

// Implement recv_frame: read the 2-byte header, then the payload.

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

	// Send framed PING, receive framed response.
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
