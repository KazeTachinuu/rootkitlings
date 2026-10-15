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

	error = sock_create_kern(current->nsproxy->net_ns,
				 AF_INET, SOCK_STREAM, IPPROTO_TCP,
				 &rk_sock);
	if (error)
		return error;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = in_aton(host);
	addr.sin_port = htons(port);

	error = kernel_connect(rk_sock, (struct sockaddr *)&addr,
			       sizeof(addr), 0);
	if (error)
		goto out_release;

	memset(&msg, 0, sizeof(msg));
	vec.iov_base = payload;
	vec.iov_len = strlen(payload);

	error = kernel_sendmsg(rk_sock, &msg, &vec, 1, vec.iov_len);
	if (error < 0)
		goto out_release;

	sock_release(rk_sock);
	rk_sock = NULL;
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
MODULE_DESCRIPTION("Kernel TCP socket connect and send");
