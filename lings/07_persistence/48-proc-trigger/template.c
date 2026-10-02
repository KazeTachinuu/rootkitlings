// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;

static int trigger_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init trigger_init(void)
{
	entry = proc_create_single("rk_privesc", 0444, NULL, trigger_show);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit trigger_exit(void)
{
	proc_remove(entry);
}

module_init(trigger_init);
module_exit(trigger_exit);

MODULE_LICENSE("GPL");
