// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <net/sock.h>

static char *host = "10.0.2.2";
module_param(host, charp, 0400);

static int port = 4444;
module_param(port, int, 0400);

static struct socket *rk_sock;

static int __init rk_init(void)
{
	struct sockaddr_in addr;
	struct kvec vec;
	struct msghdr msg;
	char payload[] = "hello\n";
	int error;

	// Create a kernel TCP socket, connect to host:port, send payload.

	return 0;

out_release:
	sock_release(rk_sock);
	rk_sock = NULL;
	return error;
}

static void __exit rk_exit(void)
{
	if (rk_sock)
		sock_release(rk_sock);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
