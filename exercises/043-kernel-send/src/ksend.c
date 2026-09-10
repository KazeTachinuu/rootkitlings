#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 4444
#define MSG         "rootkitlings hello\n"

static struct socket *sock = NULL;

static int send_message(struct socket *sock, const char *buf, size_t len)
{
	// TODO(rootkitlings): envoie buf via kernel_sendmsg.
	//
	// 1. Declare struct msghdr msg = {0};
	// 2. Declare struct kvec vec;
	// 3. vec.iov_base = (void *)buf
	// 4. vec.iov_len = len
	// 5. return kernel_sendmsg(sock, &msg, &vec, 1, len);
	//
	// 5 lignes de code. N'oublie pas le cast (void *) sur buf.

	return -1;
}

static int __init ksend_init(void)
{
	struct sockaddr_in addr;
	int ret;

	ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0) {
		pr_err("rootkitlings/043: sock_create_kern failed: %d\n", ret);
		return ret;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = in_aton(SERVER_IP);

	ret = kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
	if (ret < 0) {
		pr_err("rootkitlings/043: connect failed: %d\n", ret);
		sock_release(sock);
		sock = NULL;
		return ret;
	}

	pr_info("rootkitlings/043: connected, sending message\n");

	ret = send_message(sock, MSG, strlen(MSG));
	if (ret < 0)
		pr_err("rootkitlings/043: send failed: %d\n", ret);
	else
		pr_info("rootkitlings/043: sent %d bytes\n", ret);

	return 0;
}

static void __exit ksend_exit(void)
{
	if (sock) {
		sock_release(sock);
		sock = NULL;
	}
	pr_info("rootkitlings/043: done\n");
}

module_init(ksend_init);
module_exit(ksend_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 043: kernel_sendmsg");
