// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#define SHELL_PORT 31337

struct shell_session {
	struct file *socket_file;
	bool descriptors_installed;
};

static DEFINE_MUTEX(client_lock);
static struct proc_dir_entry *status_entry;
static struct task_struct *listener_task;
static struct socket *listener_socket;
static struct socket *active_client;
static atomic_t session_count = ATOMIC_INIT(0);
static bool stopping;

static void unpublish_client(void)
{
	mutex_lock(&client_lock);
	active_client = NULL;
	mutex_unlock(&client_lock);
}

static int prepare_stdio(struct subprocess_info *info, struct cred *new)
{
	/* TODO: install socket_file as descriptors 0, 1, and 2. */
	return -EOPNOTSUPP;
}

static void free_session(struct subprocess_info *info)
{
	struct shell_session *session = info->data;

	if (!session->descriptors_installed)
		fput(session->socket_file);
	kfree(session);
}

static int run_shell(struct socket *client)
{
	static char *argv[] = { "/bin/sh", "-i", NULL };
	static char *envp[] = { "HOME=/", "PATH=/bin:/sbin", NULL };
	struct subprocess_info *helper;
	struct shell_session *session;
	struct file *held_file;
	int error;

	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session) {
		unpublish_client();
		sock_release(client);
		return -ENOMEM;
	}
	session->socket_file = sock_alloc_file(client, 0, NULL);
	if (IS_ERR(session->socket_file)) {
		int error = PTR_ERR(session->socket_file);

		unpublish_client();
		kfree(session);
		return error;
	}
	held_file = get_file(session->socket_file);
	helper = call_usermodehelper_setup(argv[0], argv, envp, GFP_KERNEL,
					   prepare_stdio, free_session, session);
	if (!helper) {
		unpublish_client();
		fput(held_file);
		fput(session->socket_file);
		kfree(session);
		return -ENOMEM;
	}
	error = call_usermodehelper_exec(helper, UMH_WAIT_PROC);
	if (!error)
		atomic_inc(&session_count);
	unpublish_client();
	fput(held_file);
	return error;
}

static int listener_main(void *unused)
{
	while (!kthread_should_stop()) {
		struct socket *client;
		int error;

		error = kernel_accept(listener_socket, &client, O_NONBLOCK);
		if (error == -EAGAIN) {
			schedule_timeout_interruptible(HZ / 4);
			continue;
		}
		if (error)
			break;
		mutex_lock(&client_lock);
		if (stopping) {
			mutex_unlock(&client_lock);
			sock_release(client);
			break;
		}
		active_client = client;
		mutex_unlock(&client_lock);
		run_shell(client);
	}
	return 0;
}

static int status_show(struct seq_file *output, void *unused)
{
	bool active;

	mutex_lock(&client_lock);
	active = active_client != NULL;
	mutex_unlock(&client_lock);
	seq_printf(output, "port=%u\nsessions=%d\nactive=%s\n", SHELL_PORT,
		   atomic_read(&session_count), active ? "yes" : "no");
	return 0;
}

static int open_listener(void)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(SHELL_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	int error;

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP,
				 &listener_socket);
	if (error)
		return error;
	sock_set_reuseaddr(listener_socket->sk);
	error = kernel_bind(listener_socket,
			    (struct sockaddr_unsized *)&address, sizeof(address));
	if (!error)
		error = kernel_listen(listener_socket, 1);
	if (error) {
		sock_release(listener_socket);
		listener_socket = NULL;
	}
	return error;
}

static int __init shell_init(void)
{
	int error;

	error = open_listener();
	if (error)
		return error;
	status_entry = proc_create_single("rk_shell", 0400, NULL, status_show);
	if (!status_entry) {
		sock_release(listener_socket);
		return -ENOMEM;
	}
	listener_task = kthread_run(listener_main, NULL, "rk_shell");
	if (IS_ERR(listener_task)) {
		error = PTR_ERR(listener_task);
		proc_remove(status_entry);
		sock_release(listener_socket);
		return error;
	}
	return 0;
}

static void __exit shell_exit(void)
{
	mutex_lock(&client_lock);
	stopping = true;
	if (active_client)
		kernel_sock_shutdown(active_client, SHUT_RDWR);
	mutex_unlock(&client_lock);
	kernel_sock_shutdown(listener_socket, SHUT_RDWR);
	kthread_stop(listener_task);
	proc_remove(status_entry);
	sock_release(listener_socket);
}

module_init(shell_init);
module_exit(shell_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Socket-backed interactive root shell");
