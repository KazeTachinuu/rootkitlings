// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;

static int aes_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init aes_init(void)
{
	entry = proc_create_single("rk_aes", 0444, NULL, aes_show);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit aes_exit(void)
{
	proc_remove(entry);
}

module_init(aes_init);
module_exit(aes_exit);

MODULE_LICENSE("GPL");
