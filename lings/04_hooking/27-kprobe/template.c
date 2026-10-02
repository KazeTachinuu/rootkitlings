// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;
static atomic_t hit_count = ATOMIC_INIT(0);

static int probe_handler(struct kprobe *p, struct pt_regs *regs)
{
	/* TODO: count or log the hit */
	return 0;
}

static struct kprobe kp = {
	.symbol_name = "", /* TODO: target function name */
	.pre_handler = probe_handler,
};

static int probe_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init probe_init(void)
{
	int err;

	err = register_kprobe(&kp);
	if (err)
		return err;

	entry = proc_create_single("rk_probe", 0444, NULL, probe_show);
	if (!entry) {
		unregister_kprobe(&kp);
		return -ENOMEM;
	}

	return 0;
}

static void __exit probe_exit(void)
{
	proc_remove(entry);
	unregister_kprobe(&kp);
}

module_init(probe_init);
module_exit(probe_exit);

MODULE_LICENSE("GPL");
