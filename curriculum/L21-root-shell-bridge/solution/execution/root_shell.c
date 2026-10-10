// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/jiffies.h>
#include <linux/net.h>
#include <linux/pid.h>
#include <linux/sched/signal.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/umh.h>
#include <net/net_namespace.h>

#include "root_shell.h"

#define RK_SHELL_TIMEOUT_MAX_MS 300000U

static char *rk_shell_argv[] = {
	"setsid",
	"/bin/sh",
	NULL,
};

static char *rk_shell_envp[] = {
	"HOME=/",
	"PATH=/bin",
	"TERM=dumb",
	NULL,
};

static void rk_shell_terminate_locked(struct rk_root_shell *shell)
{
	if (shell->socket)
		(void)kernel_sock_shutdown(shell->socket, SHUT_RDWR);
	if (shell->leader) {
		(void)kill_pgrp(shell->leader, SIGKILL, 1);
		(void)kill_pid(shell->leader, SIGKILL, 1);
	}
}

static int rk_shell_install_fd(struct file *file, unsigned int wanted)
{
	int descriptor;

	descriptor = get_unused_fd_flags(0);
	if (descriptor < 0)
		return descriptor;
	if ((unsigned int)descriptor != wanted) {
		put_unused_fd(descriptor);
		return -EBUSY;
	}
	get_file(file);
	fd_install(descriptor, file);
	return 0;
}

static int rk_shell_helper_init(struct subprocess_info *info,
				struct cred *credentials)
{
	struct rk_root_shell *shell = info->data;
	struct pid *leader;
	unsigned int descriptor;
	int error = 0;

	(void)credentials;
	mutex_lock(&shell->lock);
	if (shell->stopping || shell->timed_out || !shell->file) {
		error = -ECANCELED;
		goto unlock;
	}
	for (descriptor = 0; descriptor < 3; descriptor++) {
		error = rk_shell_install_fd(shell->file, descriptor);
		if (error)
			goto unlock;
	}
	leader = get_task_pid(current, PIDTYPE_PID);
	if (!leader) {
		error = -ESRCH;
		goto unlock;
	}
	shell->leader = leader;

unlock:
	mutex_unlock(&shell->lock);
	return error;
}

static void rk_shell_timeout(struct work_struct *work)
{
	struct rk_root_shell *shell = container_of(
		to_delayed_work(work), struct rk_root_shell, timeout_work);

	mutex_lock(&shell->lock);
	if (shell->busy) {
		shell->timed_out = true;
		rk_shell_terminate_locked(shell);
	}
	mutex_unlock(&shell->lock);
}

static void rk_shell_work(struct work_struct *work)
{
	struct rk_root_shell *shell = container_of(
		work, struct rk_root_shell, work);
	struct sockaddr_in peer = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	struct subprocess_info *helper;
	struct socket *socket = NULL;
	struct file *file = NULL;
	struct pid *leader;
	unsigned int timeout_ms;
	int error;

	mutex_lock(&shell->lock);
	peer.sin_port = htons(shell->port);
	timeout_ms = shell->timeout_ms;
	mutex_unlock(&shell->lock);

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP,
				 &socket);
	if (error)
		goto finish;
	if (!socket) {
		error = -EPROTO;
		goto finish;
	}

	mutex_lock(&shell->lock);
	if (shell->stopping) {
		error = -ECANCELED;
		mutex_unlock(&shell->lock);
		goto finish;
	}
	shell->socket = socket;
	mutex_unlock(&shell->lock);
	mod_delayed_work(system_wq, &shell->timeout_work,
			 msecs_to_jiffies(timeout_ms));

	error = kernel_connect(socket, (struct sockaddr_unsized *)&peer,
			       sizeof(peer), 0);
	if (error)
		goto finish;

	mutex_lock(&shell->lock);
	if (shell->stopping || shell->timed_out) {
		error = -ECANCELED;
		mutex_unlock(&shell->lock);
		goto finish;
	}
	file = sock_alloc_file(socket, 0, NULL);
	if (IS_ERR(file)) {
		error = PTR_ERR(file);
		file = NULL;
		socket = NULL;
		shell->socket = NULL;
		mutex_unlock(&shell->lock);
		goto finish;
	}
	shell->file = file;
	mutex_unlock(&shell->lock);

	helper = call_usermodehelper_setup(
		"/bin/setsid", rk_shell_argv, rk_shell_envp, GFP_KERNEL,
		rk_shell_helper_init, NULL, shell);
	if (!helper) {
		error = -ENOMEM;
		goto finish;
	}
	error = call_usermodehelper_exec(helper, UMH_WAIT_PROC);

finish:
	cancel_delayed_work_sync(&shell->timeout_work);
	mutex_lock(&shell->lock);
	leader = shell->leader;
	shell->leader = NULL;
	shell->socket = NULL;
	shell->file = NULL;
	shell->busy = false;
	shell->result = error;
	mutex_unlock(&shell->lock);
	if (leader)
		put_pid(leader);
	if (file)
		fput(file);
	else if (socket)
		sock_release(socket);
}

int rk_root_shell_init(struct rk_root_shell *shell, unsigned int timeout_ms)
{
	if (!shell || !timeout_ms || timeout_ms > RK_SHELL_TIMEOUT_MAX_MS)
		return -EINVAL;
	memset(shell, 0, sizeof(*shell));
	mutex_init(&shell->lock);
	INIT_WORK(&shell->work, rk_shell_work);
	INIT_DELAYED_WORK(&shell->timeout_work, rk_shell_timeout);
	shell->timeout_ms = timeout_ms;
	shell->initialized = true;
	return 0;
}

int rk_root_shell_request(struct rk_root_shell *shell, u16 port)
{
	int error = 0;

	if (!shell || !port)
		return -EINVAL;
	mutex_lock(&shell->lock);
	if (!shell->initialized || shell->stopping)
		error = -ESHUTDOWN;
	else if (shell->busy)
		error = -EBUSY;
	else {
		shell->busy = true;
		shell->timed_out = false;
		shell->result = -EINPROGRESS;
		shell->port = port;
		queue_work(system_wq, &shell->work);
	}
	mutex_unlock(&shell->lock);
	return error;
}

void rk_root_shell_stop(struct rk_root_shell *shell)
{
	if (!shell || !READ_ONCE(shell->initialized))
		return;
	mutex_lock(&shell->lock);
	shell->stopping = true;
	rk_shell_terminate_locked(shell);
	mutex_unlock(&shell->lock);
	cancel_delayed_work_sync(&shell->timeout_work);
	cancel_work_sync(&shell->work);
	mutex_lock(&shell->lock);
	shell->busy = false;
	shell->initialized = false;
	mutex_unlock(&shell->lock);
}
