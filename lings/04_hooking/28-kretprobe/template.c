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

static int ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/* TODO: inspect return value via regs_return_value(regs) */
	return 0;
}

static struct kretprobe krp = {
	.handler     = ret_handler,
	.kp.symbol_name = "", /* TODO: target function name */
	.maxactive   = 8,
};

static int retprobe_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init retprobe_init(void)
{
	int err;

	err = register_kretprobe(&krp);
	if (err)
		return err;

	entry = proc_create_single("rk_retprobe", 0444, NULL, retprobe_show);
	if (!entry) {
		unregister_kretprobe(&krp);
		return -ENOMEM;
	}

	return 0;
}

static void __exit retprobe_exit(void)
{
	proc_remove(entry);
	unregister_kretprobe(&krp);
}

module_init(retprobe_init);
module_exit(retprobe_exit);

MODULE_LICENSE("GPL");
