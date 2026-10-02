// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;

static int task_info_show(struct seq_file *m, void *v)
{
	seq_printf(m, "pid=%d comm=%s\n", current->pid, current->comm);
	return 0;
}

static int __init task_inspect_init(void)
{
	entry = proc_create_single("rk_task", 0444, NULL, task_info_show);
	if (!entry)
		return -ENOMEM;

	pr_info("rk: introspection ready\n");
	return 0;
}

static void __exit task_inspect_exit(void)
{
	proc_remove(entry);
}

module_init(task_inspect_init);
module_exit(task_inspect_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Task introspection via current");
