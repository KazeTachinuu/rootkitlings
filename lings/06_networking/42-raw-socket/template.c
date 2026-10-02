// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <net/sock.h>

static struct proc_dir_entry *entry;

static int rawsock_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int rawsock_open(struct inode *inode, struct file *file)
{
	return single_open(file, rawsock_show, NULL);
}

static ssize_t rawsock_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *position)
{
	/* TODO: send a raw packet on write */
	return count;
}

static const struct proc_ops rawsock_ops = {
	.proc_open    = rawsock_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = rawsock_write,
};

static int __init rawsock_init(void)
{
	entry = proc_create("rk_rawsock", 0666, NULL, &rawsock_ops);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit rawsock_exit(void)
{
	proc_remove(entry);
}

module_init(rawsock_init);
module_exit(rawsock_exit);

MODULE_LICENSE("GPL");
