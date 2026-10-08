// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define ENTRY_SIZE 64
#define ENTRY_MAX  32

struct registry_entry {
	char text[ENTRY_SIZE];
	struct list_head node;
};

static LIST_HEAD(entries);
static DEFINE_MUTEX(entries_lock);
static size_t entry_count;
static struct proc_dir_entry *proc_entry;

static int registry_show(struct seq_file *m, void *unused)
{
	mutex_lock(&entries_lock);
	/* Iterate over entries in insertion order and print each text field. */
	mutex_unlock(&entries_lock);
	return 0;
}

static int registry_open(struct inode *inode, struct file *file)
{
	return single_open(file, registry_show, NULL);
}

static ssize_t registry_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *position)
{
	struct registry_entry *entry;

	if (!count)
		return -EINVAL;
	if (count >= ENTRY_SIZE)
		return -EINVAL;

	entry = kmalloc_obj(*entry, GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	if (copy_from_user(entry->text, buffer, count)) {
		kfree(entry);
		return -EFAULT;
	}
	if (memchr(entry->text, '\0', count)) {
		kfree(entry);
		return -EINVAL;
	}

	entry->text[count] = '\0';
	if (count && entry->text[count - 1] == '\n')
		entry->text[count - 1] = '\0';

	mutex_lock(&entries_lock);
	if (entry_count == ENTRY_MAX) {
		mutex_unlock(&entries_lock);
		kfree(entry);
		return -ENOSPC;
	}
	/* Append entry->node to entries. */
	entry_count++;
	mutex_unlock(&entries_lock);
	return count;
}

static const struct proc_ops registry_ops = {
	.proc_open = registry_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = registry_write,
};

static void purge_entries(void)
{
	mutex_lock(&entries_lock);
	/* Unlink and free every entry with the safe iterator. */
	entry_count = 0;
	mutex_unlock(&entries_lock);
}

static int __init registry_init(void)
{
	proc_entry = proc_create("rk_registry", 0600, NULL, &registry_ops);
	return proc_entry ? 0 : -ENOMEM;
}

static void __exit registry_exit(void)
{
	proc_remove(proc_entry);
	purge_entries();
}

module_init(registry_init);
module_exit(registry_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Store records in a kernel linked list");
