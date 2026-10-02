// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;
static unsigned long resolved_addr;

static unsigned long resolve_symbol(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long addr;

	if (register_kprobe(&kp) < 0)
		return 0;
	addr = (unsigned long)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

static int resolved_addr_show(struct seq_file *m, void *v)
{
	seq_printf(m, "commit_creds=0x%lx\n", resolved_addr);
	return 0;
}

static int __init symbol_init(void)
{
	resolved_addr = resolve_symbol("commit_creds");
	if (!resolved_addr) {
		pr_err("rk: failed to resolve commit_creds\n");
		return -ENOENT;
	}

	pr_info("rk: resolved commit_creds at 0x%lx\n", resolved_addr);

	entry = proc_create_single("rk_symbols", 0444, NULL, resolved_addr_show);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit symbol_exit(void)
{
	proc_remove(entry);
}

module_init(symbol_init);
module_exit(symbol_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kprobe-based symbol resolution");
