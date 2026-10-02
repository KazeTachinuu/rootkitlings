// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

static struct proc_dir_entry *entry;

static int store_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int store_open(struct inode *inode, struct file *file)
{
	return single_open(file, store_show, NULL);
}

static ssize_t store_write(struct file *file, const char __user *buffer,
			   size_t count, loff_t *position)
{
	/* TODO: store the written value */
	return count;
}

static const struct proc_ops store_ops = {
	.proc_open    = store_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = store_write,
};

static int __init store_init(void)
{
	entry = proc_create("rk_store", 0666, NULL, &store_ops);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit store_exit(void)
{
	proc_remove(entry);
}

module_init(store_init);
module_exit(store_exit);

MODULE_LICENSE("GPL");
