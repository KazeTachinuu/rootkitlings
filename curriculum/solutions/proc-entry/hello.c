// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_entry;

static const struct proc_ops entry_ops = {};

static int __init entry_init(void)
{
	proc_entry = proc_create("rk_entry", 0444, NULL, &entry_ops);
	return proc_entry ? 0 : -ENOMEM;
}

static void __exit entry_exit(void)
{
	proc_remove(proc_entry);
}

module_init(entry_init);
module_exit(entry_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Publish and remove a procfs entry");
