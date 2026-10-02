// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

struct log_entry {
	char text[64];
	struct list_head node;
};

static LIST_HEAD(entries);
static struct proc_dir_entry *proc_entry;

static ssize_t entries_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	struct log_entry *e;
	char out[512];
	int len = 0;

	list_for_each_entry(e, &entries, node)
		len += snprintf(out + len, sizeof(out) - len, "%s\n", e->text);

	return simple_read_from_buffer(buf, count, pos, out, len);
}

static ssize_t entries_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	struct log_entry *e;

	if (count >= sizeof(e->text))
		return -EINVAL;

	e = kmalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	if (copy_from_user(e->text, buf, count)) {
		kfree(e);
		return -EFAULT;
	}
	e->text[count] = '\0';
	if (count > 0 && e->text[count - 1] == '\n')
		e->text[count - 1] = '\0';

	list_add_tail(&e->node, &entries);
	pr_info("rk: added entry: %s\n", e->text);
	return count;
}

static const struct proc_ops entries_proc_ops = {
	.proc_read = entries_read,
	.proc_write = entries_write,
};

static void purge_entries(void)
{
	struct log_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, &entries, node) {
		list_del(&e->node);
		kfree(e);
	}
}

static int __init registry_init(void)
{
	proc_entry = proc_create("rk_registry", 0666, NULL, &entries_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	pr_info("rk: list interface ready\n");
	return 0;
}

static void __exit registry_exit(void)
{
	purge_entries();
	proc_remove(proc_entry);
	pr_info("rk: cleaned up\n");
}

module_init(registry_init);
module_exit(registry_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel linked list manipulation");
