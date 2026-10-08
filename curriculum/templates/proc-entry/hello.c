// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_entry;

static const struct proc_ops entry_ops = {};

static int __init entry_init(void)
{
	(void)proc_entry;
	(void)entry_ops;
	/* Publish /proc/rk_entry and save the returned entry. */
	return 0;
}

static void __exit entry_exit(void)
{
	/* Remove the entry before the module's memory disappears. */
}

module_init(entry_init);
module_exit(entry_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Publish and remove a procfs entry");
