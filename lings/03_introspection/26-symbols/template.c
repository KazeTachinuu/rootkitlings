// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;

static unsigned long lookup_name(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long addr;

	if (register_kprobe(&kp) < 0)
		return 0;
	addr = (unsigned long)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

static int symbols_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init symbols_init(void)
{
	entry = proc_create_single("rk_symbols", 0444, NULL, symbols_show);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit symbols_exit(void)
{
	proc_remove(entry);
}

module_init(symbols_init);
module_exit(symbols_exit);

MODULE_LICENSE("GPL");
