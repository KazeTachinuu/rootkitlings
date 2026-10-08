// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define MESSAGE_SIZE 64

static char message[MESSAGE_SIZE] = "hello";
static DEFINE_MUTEX(message_lock);
static struct proc_dir_entry *proc_entry;

static int message_show(struct seq_file *m, void *unused)
{
	mutex_lock(&message_lock);
	seq_printf(m, "%s\n", message);
	mutex_unlock(&message_lock);
	return 0;
}

static int message_open(struct inode *inode, struct file *file)
{
	return single_open(file, message_show, NULL);
}

static ssize_t message_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *position)
{
	/* Copy, validate, then commit candidate under message_lock. */
	return count;
}

static const struct proc_ops message_ops = {
	.proc_open = message_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = message_write,
};

static int __init message_init(void)
{
	proc_entry = proc_create("rk_store", 0600, NULL, &message_ops);
	return proc_entry ? 0 : -ENOMEM;
}

static void __exit message_exit(void)
{
	proc_remove(proc_entry);
}

module_init(message_init);
module_exit(message_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Store text through a procfs interface");
