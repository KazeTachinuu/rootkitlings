// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_entry;

static const char payload[] = "hello\n";

static ssize_t hello_proc_read(struct file *file, char __user *buf,
			size_t count, loff_t *pos)
{
	ssize_t copied;

	copied = simple_read_from_buffer(buf, count, pos,
					 payload, sizeof(payload) - 1);
	pr_info("rk: read count=%zu copied=%zd pos=%lld\n",
		count, copied, *pos);
	return copied;
}

static const struct proc_ops hello_proc_ops = {
	.proc_read = hello_proc_read,
};

static int __init reader_init(void)
{
	proc_entry = proc_create("rk_reader", 0444, NULL, &hello_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	pr_info("rk: published /proc/rk_reader\n");
	return 0;
}

static void __exit reader_exit(void)
{
	proc_remove(proc_entry);
	pr_info("rk: removed /proc/rk_reader\n");
}

module_init(reader_init);
module_exit(reader_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Procfs read interface");
