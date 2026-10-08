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
static atomic_long_t read_count = ATOMIC_LONG_INIT(0);
static atomic_long_t write_count = ATOMIC_LONG_INIT(0);
static bool fail_second;
module_param(fail_second, bool, 0600);
MODULE_PARM_DESC(fail_second, "fail after the first probe to test rollback");

static int read_handler(struct kprobe *p, struct pt_regs *regs)
{
	atomic_long_inc(&read_count);
	return 0;
}

static int write_handler(struct kprobe *p, struct pt_regs *regs)
{
	atomic_long_inc(&write_count);
	return 0;
}

static struct kprobe kp_read = {
	.symbol_name = "vfs_read",
	.pre_handler = read_handler,
};

static struct kprobe kp_write = {
	.symbol_name = "vfs_write",
	.pre_handler = write_handler,
};

static int lifecycle_read(struct seq_file *m, void *v)
{
	seq_printf(m, "vfs_read=%ld vfs_write=%ld\n",
		   atomic_long_read(&read_count),
		   atomic_long_read(&write_count));
	return 0;
}

static int __init lifecycle_init(void)
{
	int err;

	err = register_kprobe(&kp_read);
	if (err) {
		pr_err("rk: kp_read failed: %d\n", err);
		return err;
	}

	err = fail_second ? -EINVAL : register_kprobe(&kp_write);
	if (err) {
		pr_err("rk: kp_write failed: %d\n", err);
		unregister_kprobe(&kp_read);
		return err;
	}

	entry = proc_create_single("rk_lifecycle", 0444, NULL, lifecycle_read);
	if (!entry) {
		unregister_kprobe(&kp_write);
		unregister_kprobe(&kp_read);
		return -ENOMEM;
	}

	pr_info("rk: hooks installed on vfs_read and vfs_write\n");
	return 0;
}

static void __exit lifecycle_exit(void)
{
	proc_remove(entry);
	unregister_kprobe(&kp_write);
	unregister_kprobe(&kp_read);
	pr_info("rk: hooks removed, read=%ld write=%ld\n",
		atomic_long_read(&read_count), atomic_long_read(&write_count));
}

module_init(lifecycle_init);
module_exit(lifecycle_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hook installation and removal lifecycle");
