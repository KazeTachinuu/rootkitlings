// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;

static int errptr_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init errptr_init(void)
{
	entry = proc_create_single("rk_errptr", 0444, NULL, errptr_show);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit errptr_exit(void)
{
	proc_remove(entry);
}

module_init(errptr_init);
module_exit(errptr_exit);

MODULE_LICENSE("GPL");
