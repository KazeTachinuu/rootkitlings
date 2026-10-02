// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;
static struct list_head *saved_prev;

static int cloak_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init cloak_init(void)
{
	entry = proc_create_single("rk_cloak", 0444, NULL, cloak_show);
	if (!entry)
		return -ENOMEM;

	/* TODO: list_del to hide from /proc/modules */
	return 0;
}

static void __exit cloak_exit(void)
{
	/* TODO: re-add to module list for clean unload */
	proc_remove(entry);
}

module_init(cloak_init);
module_exit(cloak_exit);

MODULE_LICENSE("GPL");
