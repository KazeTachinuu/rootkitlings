// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *entry;
static unsigned long open_count;

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	open_count++;
	return 0;
}

static struct kprobe kp = {
	.symbol_name = "do_sys_openat2",
	.pre_handler = handler_pre,
};

static ssize_t intercept_read(struct file *file, char __user *buffer,
			       size_t count, loff_t *position)
{
	char buf[32];
	int len;

	len = snprintf(buf, sizeof(buf), "%lu\n", open_count);
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops intercept_ops = {
	.proc_read = intercept_read,
};

static int __init probe_init(void)
{
	int err;

	err = register_kprobe(&kp);
	if (err) {
		pr_err("rk: kprobe registration failed: %d\n", err);
		return err;
	}

	pr_info("rk: kprobe registered on %s\n", kp.symbol_name);

	entry = proc_create("rk_probe", 0444, NULL, &intercept_ops);
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
	pr_info("rk: kprobe removed, %lu opens intercepted\n", open_count);
}

module_init(probe_init);
module_exit(probe_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Syscall interception via kprobe");
