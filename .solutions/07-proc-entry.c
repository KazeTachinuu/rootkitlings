// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_entry;

static const struct proc_ops hello_proc_ops = {
};

static int __init proc_init(void)
{
	proc_entry = proc_create("rk_entry", 0444, NULL, &hello_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	pr_info("rk: published /proc/rk_entry\n");
	return 0;
}

static void __exit proc_exit(void)
{
	proc_remove(proc_entry);
	pr_info("rk: removed /proc/rk_entry\n");
}

module_init(proc_init);
module_exit(proc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Procfs entry creation with proc_ops");
