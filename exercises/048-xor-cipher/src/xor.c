#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 4444
#define BUF_SIZE    256
#define XOR_KEY     "rootkitlings"
#define XOR_KEY_LEN 12

static struct socket *sock = NULL;

static void xor_encrypt(char *buf, size_t len, const char *key, size_t key_len)
{
	// TODO(rootkitlings): XOR chaque octet de buf avec la cle (en boucle).
	//
	// Pour chaque i de 0 a len-1:
	//   buf[i] = buf[i] ^ key[i % key_len];
	//
	// 3 lignes (une boucle for avec une ligne dedans).

}

static void xor_decrypt(char *buf, size_t len, const char *key, size_t key_len)
{
	// TODO(rootkitlings): identique a xor_encrypt (XOR est symetrique).
	//
	// Appelle xor_encrypt ou re-ecris la meme boucle.
	// 1 ligne si tu appelles xor_encrypt, 3 si tu re-ecris.

}

static int ksend(struct socket *s, const void *buf, size_t len)
{
	struct msghdr msg = {0};
	struct kvec vec = { .iov_base = (void *)buf, .iov_len = len };

	return kernel_sendmsg(s, &msg, &vec, 1, len);
}

static int krecv(struct socket *s, void *buf, size_t len)
{
	struct msghdr msg = {0};
	struct kvec vec = { .iov_base = buf, .iov_len = len };

	return kernel_recvmsg(s, &msg, &vec, 1, len, 0);
}

static int __init xor_init(void)
{
	struct sockaddr_in addr;
	char buf[BUF_SIZE];
	char reply[] = "xor works";
	int ret;

	ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0)
		return ret;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = in_aton(SERVER_IP);

	ret = kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
	if (ret < 0) {
		sock_release(sock);
		sock = NULL;
		return ret;
	}

	/* Receive XOR-encrypted message from server */
	memset(buf, 0, BUF_SIZE);
	ret = krecv(sock, buf, BUF_SIZE - 1);
	if (ret > 0) {
		pr_info("rootkitlings/048: received %d encrypted bytes\n", ret);
		xor_decrypt(buf, ret, XOR_KEY, XOR_KEY_LEN);
		buf[ret] = '\0';
		pr_info("rootkitlings/048: decrypted: %s\n", buf);
	}

	/* Send XOR-encrypted reply */
	xor_encrypt(reply, strlen(reply), XOR_KEY, XOR_KEY_LEN);
	ksend(sock, reply, strlen("xor works"));
	pr_info("rootkitlings/048: sent encrypted reply\n");

	return 0;
}

static void __exit xor_exit(void)
{
	if (sock) {
		sock_release(sock);
		sock = NULL;
	}
	pr_info("rootkitlings/048: done\n");
}

module_init(xor_init);
module_exit(xor_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 048: XOR cipher");
