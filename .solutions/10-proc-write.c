// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

static struct proc_dir_entry *proc_entry;
static char command[64] = "hello";

static ssize_t command_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	char out[72];
	int len;

	len = snprintf(out, sizeof(out), "%s\n", command);
	return simple_read_from_buffer(buf, count, pos, out, len);
}

static ssize_t command_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	if (count >= sizeof(command))
		return -EINVAL;
	if (copy_from_user(command, buf, count))
		return -EFAULT;
	command[count] = '\0';
	if (count > 0 && command[count - 1] == '\n')
		command[count - 1] = '\0';
	pr_info("rk: message set to %s\n", command);
	return count;
}

static const struct proc_ops command_proc_ops = {
	.proc_read = command_read,
	.proc_write = command_write,
};

static int __init store_init(void)
{
	proc_entry = proc_create("rk_store", 0666, NULL, &command_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	pr_info("rk: published /proc/rk_store\n");
	return 0;
}

static void __exit store_exit(void)
{
	proc_remove(proc_entry);
	pr_info("rk: removed /proc/rk_store\n");
}

module_init(store_init);
module_exit(store_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Procfs command interface");
