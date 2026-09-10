#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 4444

static struct socket *sock = NULL;

static int __init ksock_init(void)
{
	struct sockaddr_in addr;
	int ret;

	pr_info("rootkitlings/042: connecting to %s:%d\n", SERVER_IP, SERVER_PORT);

	// TODO(rootkitlings): cree un socket TCP kernel.
	//
	// Appelle sock_create_kern avec:
	//   - &init_net (namespace reseau)
	//   - AF_INET (IPv4)
	//   - SOCK_STREAM (TCP)
	//   - IPPROTO_TCP
	//   - &sock (pointeur vers la variable globale)
	//
	// 1 ligne, verifie le retour.

	if (ret < 0) {
		pr_err("rootkitlings/042: sock_create_kern failed: %d\n", ret);
		return ret;
	}

	// TODO(rootkitlings): remplis struct sockaddr_in et connecte.
	//
	// 1. memset(&addr, 0, sizeof(addr))
	// 2. addr.sin_family = AF_INET
	// 3. addr.sin_port = htons(SERVER_PORT)
	// 4. addr.sin_addr.s_addr = in_aton(SERVER_IP)
	// 5. kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0)
	//
	// 5 lignes.

	if (ret < 0) {
		pr_err("rootkitlings/042: kernel_connect failed: %d\n", ret);
		sock_release(sock);
		sock = NULL;
		return ret;
	}

	pr_info("rootkitlings/042: connected\n");
	return 0;
}

static void __exit ksock_exit(void)
{
	if (sock) {
		sock_release(sock);
		sock = NULL;
	}
	pr_info("rootkitlings/042: socket closed\n");
}

module_init(ksock_init);
module_exit(ksock_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 042: kernel TCP socket");
