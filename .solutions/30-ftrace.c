// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_entry;
static unsigned long open_count;
static unsigned long target_addr;

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

static void notrace count_opens(unsigned long ip, unsigned long parent_ip,
				struct ftrace_ops *ops,
				struct ftrace_regs *fregs)
{
	open_count++;
}

static struct ftrace_ops hook_ops = {
	.func  = count_opens,
	.flags = FTRACE_OPS_FL_SAVE_REGS
	       | FTRACE_OPS_FL_IPMODIFY
	       | FTRACE_OPS_FL_RECURSION,
};

static int install_hook(void)
{
	int err;

	target_addr = lookup_name("do_sys_openat2");
	if (!target_addr)
		return -ENOENT;

	err = ftrace_set_filter_ip(&hook_ops, target_addr, 0, 0);
	if (err)
		return err;

	err = register_ftrace_function(&hook_ops);
	if (err) {
		ftrace_set_filter_ip(&hook_ops, target_addr, 1, 0);
		return err;
	}
	return 0;
}

static void remove_hook(void)
{
	unregister_ftrace_function(&hook_ops);
	ftrace_set_filter_ip(&hook_ops, target_addr, 1, 0);
}

static ssize_t counter_show(struct file *file, char __user *buffer,
			    size_t count, loff_t *position)
{
	char buf[32];
	int len;

	len = snprintf(buf, sizeof(buf), "%lu\n", open_count);
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops counter_ops = {
	.proc_read = counter_show,
};

static int __init ftrace_hook_init(void)
{
	int err;

	err = install_hook();
	if (err)
		return err;

	proc_entry = proc_create("rk_ftrace", 0444, NULL, &counter_ops);
	if (!proc_entry) {
		remove_hook();
		return -ENOMEM;
	}

	pr_info("rk: ftrace hook installed on do_sys_openat2\n");
	return 0;
}

static void __exit ftrace_hook_exit(void)
{
	proc_remove(proc_entry);
	remove_hook();
	pr_info("rk: ftrace hook removed, %lu opens\n", open_count);
}

module_init(ftrace_hook_init);
module_exit(ftrace_hook_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ftrace-based function hooking");
