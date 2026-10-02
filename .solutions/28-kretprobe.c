// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_entry;
static unsigned long success_count;
static unsigned long fail_count;

static int tally_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	long retval = regs_return_value(regs);

	if (retval < 0)
		fail_count++;
	else
		success_count++;

	return 0;
}

static struct kretprobe krp = {
	.handler        = tally_return,
	.maxactive      = 20,
	.kp.symbol_name = "vfs_read",
};

static ssize_t stats_show(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "success=%lu failed=%lu\n",
		       success_count, fail_count);
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops stats_ops = {
	.proc_read = stats_show,
};

static int __init retprobe_init(void)
{
	int err;

	err = register_kretprobe(&krp);
	if (err) {
		pr_err("rk: kretprobe registration failed: %d\n", err);
		return err;
	}

	pr_info("rk: kretprobe registered on %s\n", krp.kp.symbol_name);

	proc_entry = proc_create("rk_retprobe", 0444, NULL, &stats_ops);
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
