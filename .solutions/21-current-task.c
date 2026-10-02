// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>

static struct proc_dir_entry *entry;

static ssize_t task_info_show(struct file *file, char __user *buffer,
			      size_t count, loff_t *position)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "pid=%d comm=%s\n",
		       current->pid, current->comm);
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops task_proc_ops = {
	.proc_read = task_info_show,
};

static int __init task_inspect_init(void)
{
	entry = proc_create("rk_task", 0444, NULL, &task_proc_ops);
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
