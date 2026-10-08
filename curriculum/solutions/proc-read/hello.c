// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

static const char payload[] = "hello\n";
static struct proc_dir_entry *proc_entry;

static ssize_t entry_read(struct file *file, char __user *buffer, size_t count,
			  loff_t *position)
{
	return simple_read_from_buffer(buffer, count, position, payload,
				       sizeof(payload) - 1);
}

static const struct proc_ops entry_ops = {
	.proc_read = entry_read,
};

static int __init entry_init(void)
{
	proc_entry = proc_create("rk_reader", 0444, NULL, &entry_ops);
	return proc_entry ? 0 : -ENOMEM;
}

static void __exit entry_exit(void)
{
	proc_remove(proc_entry);
}

module_init(entry_init);
module_exit(entry_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read bytes from a procfs entry");
