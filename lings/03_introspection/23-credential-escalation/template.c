// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

static struct proc_dir_entry *entry;

static int escalate_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int escalate_open(struct inode *inode, struct file *file)
{
	return single_open(file, escalate_show, NULL);
}

static ssize_t escalate_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *position)
{
	/* TODO: on magic string, escalate to root */
	return count;
}

static const struct proc_ops escalate_ops = {
	.proc_open    = escalate_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = escalate_write,
};

static int __init escalate_init(void)
{
	entry = proc_create("rk_escalate", 0666, NULL, &escalate_ops);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit escalate_exit(void)
{
	proc_remove(entry);
}

module_init(escalate_init);
module_exit(escalate_exit);

MODULE_LICENSE("GPL");
