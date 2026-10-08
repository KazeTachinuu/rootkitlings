// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *proc_entry;
static atomic_long_t success_count = ATOMIC_LONG_INIT(0);
static atomic_long_t fail_count = ATOMIC_LONG_INIT(0);

static int tally_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	long retval = regs_return_value(regs);

	if (retval < 0)
		atomic_long_inc(&fail_count);
	else
		atomic_long_inc(&success_count);

	return 0;
}

static struct kretprobe krp = {
	.handler = tally_return,
	.kp.symbol_name = "vfs_read",
};

static int stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "success=%ld failed=%ld\n",
		   atomic_long_read(&success_count),
		   atomic_long_read(&fail_count));
	return 0;
}

static int __init retprobe_init(void)
{
	int err;

	err = register_kretprobe(&krp);
	if (err) {
		pr_err("rk: kretprobe registration failed: %d\n", err);
		return err;
	}

	pr_info("rk: kretprobe registered on %s\n", krp.kp.symbol_name);

	proc_entry = proc_create_single("rk_retprobe", 0444, NULL, stats_show);
	if (!proc_entry) {
		unregister_kretprobe(&krp);
		return -ENOMEM;
	}

	return 0;
}

static void __exit retprobe_exit(void)
{
	proc_remove(proc_entry);
	unregister_kretprobe(&krp);
	pr_info("rk: kretprobe removed\n");
}

module_init(retprobe_init);
module_exit(retprobe_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Return value interception via kretprobe");
