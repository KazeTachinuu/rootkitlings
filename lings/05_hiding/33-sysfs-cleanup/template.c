// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;

static int sysfs_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init cloak_init(void)
{
	entry = proc_create_single("rk_sysfs", 0444, NULL, sysfs_show);
	if (!entry)
		return -ENOMEM;

	/* TODO: list_del + kobject_del to hide from lsmod and /sys/module */
	return 0;
}

static void __exit cloak_exit(void)
{
	proc_remove(entry);
}

module_init(cloak_init);
module_exit(cloak_exit);

MODULE_LICENSE("GPL");
