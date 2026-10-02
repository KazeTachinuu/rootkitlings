// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/in.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/net_namespace.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/socket.h>

static struct proc_dir_entry *entry;
static struct socket *raw_sock;

static ssize_t sock_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "socket=%s\ntype=RAW\nproto=ICMP\n",
		       raw_sock ? "open" : "closed");
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops sock_proc_ops = {
	.proc_read = sock_read,
};

static int __init rawsock_init(void)
{
	int err;

	err = sock_create_kern(&init_net, AF_INET, SOCK_RAW, IPPROTO_ICMP,
			       &raw_sock);
	if (err) {
		pr_err("rk: sock_create_kern failed: %d\n", err);
		return err;
	}

	entry = proc_create("rk_rawsock", 0444, NULL, &sock_proc_ops);
	if (!entry) {
		sock_release(raw_sock);
		raw_sock = NULL;
		return -ENOMEM;
	}

	pr_info("rk: raw ICMP socket created in kernel\n");
	return 0;
}

static void __exit rawsock_exit(void)
{
	proc_remove(entry);
	if (raw_sock) {
		sock_release(raw_sock);
		raw_sock = NULL;
	}
}

module_init(rawsock_init);
module_exit(rawsock_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Raw kernel socket creation");
