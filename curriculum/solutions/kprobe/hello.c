// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;
static atomic_long_t open_count = ATOMIC_LONG_INIT(0);

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	atomic_long_inc(&open_count);
	return 0;
}

static struct kprobe kp = {
	.symbol_name = "do_sys_openat2",
	.pre_handler = handler_pre,
};

static int intercept_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%ld\n", atomic_long_read(&open_count));
	return 0;
}

static int __init probe_init(void)
{
	int err;

	err = register_kprobe(&kp);
	if (err) {
		pr_err("rk: kprobe registration failed: %d\n", err);
		return err;
	}

	pr_info("rk: kprobe registered on %s\n", kp.symbol_name);

	entry = proc_create_single("rk_probe", 0444, NULL, intercept_show);
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
	pr_info("rk: kprobe removed, %ld opens intercepted\n",
		atomic_long_read(&open_count));
}

module_init(probe_init);
module_exit(probe_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Syscall interception via kprobe");
