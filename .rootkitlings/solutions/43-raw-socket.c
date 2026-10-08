// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/socket.h>
#include <net/net_namespace.h>

static struct proc_dir_entry *entry;
static struct socket *raw_sock;

static int sock_read(struct seq_file *m, void *v)
{
	seq_printf(m, "socket=%s\ntype=RAW\nproto=ICMP\n",
		   raw_sock ? "open" : "closed");
	return 0;
}

static int __init rawsock_init(void)
{
	int err;

	err = sock_create_kern(&init_net, AF_INET, SOCK_RAW, IPPROTO_ICMP,
			       &raw_sock);
	if (err) {
		pr_err("rk: sock_create_kern failed: %d\n", err);
		return err;
	}

	entry = proc_create_single("rk_rawsock", 0444, NULL, sock_read);
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
