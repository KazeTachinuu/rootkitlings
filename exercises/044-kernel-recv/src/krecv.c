#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 4444
#define HELLO_MSG   "rootkitlings hello\n"
#define BUF_SIZE    256

static struct socket *sock = NULL;

static int send_msg(struct socket *sock, const char *buf, size_t len)
{
	struct msghdr msg = {0};
	struct kvec vec;

	vec.iov_base = (void *)buf;
	vec.iov_len = len;
	return kernel_sendmsg(sock, &msg, &vec, 1, len);
}

static int recv_msg(struct socket *sock, char *buf, size_t len)
{
	// TODO(rootkitlings): recois des donnees via kernel_recvmsg.
	//
	// 1. Declare struct msghdr msg = {0};
	// 2. Declare struct kvec vec;
	// 3. vec.iov_base = buf
	// 4. vec.iov_len = len
	// 5. return kernel_recvmsg(sock, &msg, &vec, 1, len, 0);
	//
	// 5 lignes. Le dernier argument (0) = mode bloquant.

	return -1;
}

static int __init krecv_init(void)
{
	struct sockaddr_in addr;
	char buf[BUF_SIZE];
	int ret;

	ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0) {
		pr_err("rootkitlings/044: sock_create_kern failed: %d\n", ret);
		return ret;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = in_aton(SERVER_IP);

	ret = kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
	if (ret < 0) {
		pr_err("rootkitlings/044: connect failed: %d\n", ret);
		sock_release(sock);
		sock = NULL;
		return ret;
	}

	pr_info("rootkitlings/044: connected, sending hello\n");
	send_msg(sock, HELLO_MSG, strlen(HELLO_MSG));

	pr_info("rootkitlings/044: waiting for server response\n");
	memset(buf, 0, BUF_SIZE);
	ret = recv_msg(sock, buf, BUF_SIZE - 1);
	if (ret > 0) {
		buf[ret] = '\0';
		pr_info("rootkitlings/044: received: %s\n", buf);
	} else {
		pr_err("rootkitlings/044: recv failed or empty: %d\n", ret);
	}

	return 0;
}

static void __exit krecv_exit(void)
{
	if (sock) {
		sock_release(sock);
		sock = NULL;
	}
	pr_info("rootkitlings/044: done\n");
}

module_init(krecv_init);
module_exit(krecv_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 044: kernel_recvmsg");
