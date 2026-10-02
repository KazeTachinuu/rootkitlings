// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *entry;
static unsigned long read_count;
static unsigned long write_count;

static int read_handler(struct kprobe *p, struct pt_regs *regs)
{
	read_count++;
	return 0;
}

static int write_handler(struct kprobe *p, struct pt_regs *regs)
{
	write_count++;
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

static ssize_t lifecycle_read(struct file *file, char __user *buffer,
			       size_t count, loff_t *position)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "vfs_read=%lu vfs_write=%lu\n",
		       read_count, write_count);
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops lifecycle_ops = {
	.proc_read = lifecycle_read,
};

static int __init lifecycle_init(void)
{
	int err;

	err = register_kprobe(&kp_read);
	if (err) {
		pr_err("rk: kp_read failed: %d\n", err);
		return err;
	}

	err = register_kprobe(&kp_write);
	if (err) {
		pr_err("rk: kp_write failed: %d\n", err);
		unregister_kprobe(&kp_read);
		return err;
	}

	entry = proc_create("rk_lifecycle", 0444, NULL, &lifecycle_ops);
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
	pr_info("rk: hooks removed, read=%lu write=%lu\n",
		read_count, write_count);
}

module_init(lifecycle_init);
module_exit(lifecycle_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hook installation and removal lifecycle");
