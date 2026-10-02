// SPDX-License-Identifier: GPL-2.0
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define TARGET_NAME "rk_target"
#define REDACT_FROM "SECRET"
#define REDACT_TO   "XXXXXX"
#define REDACT_LEN  6
#define ENDBR_SIZE  4

static struct proc_dir_entry *proc_entry;
static atomic_t redact_count = ATOMIC_INIT(0);
static unsigned long sys_read_addr;
static struct ftrace_ops read_ops;

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

typedef long (*sys_read_t)(const struct pt_regs *);
static sys_read_t orig_sys_read;

static bool is_target_file(unsigned int fd)
{
	struct fd f;
	const char *name;
	bool match;

	f = fdget(fd);
	if (!fd_file(f))
		return false;

	if (!fd_file(f)->f_path.dentry) {
		fdput(f);
		return false;
	}

	name = fd_file(f)->f_path.dentry->d_name.name;
	match = strcmp(name, TARGET_NAME) == 0;
	fdput(f);
	return match;
}

static void redact_buffer(char __user *ubuf, long len)
{
	char *kbuf, *p;

	kbuf = kmalloc(len + 1, GFP_KERNEL);
	if (!kbuf)
		return;

	if (copy_from_user(kbuf, ubuf, len)) {
		kfree(kbuf);
		return;
	}
	kbuf[len] = '\0';

	p = kbuf;
	while ((p = strstr(p, REDACT_FROM)) != NULL) {
		if (p - kbuf + REDACT_LEN > len)
			break;
		memcpy(p, REDACT_TO, REDACT_LEN);
		atomic_inc(&redact_count);
		p += REDACT_LEN;
	}

	if (copy_to_user(ubuf, kbuf, len))
		;
	kfree(kbuf);
}

static long noinline intercepted_read(const struct pt_regs *regs)
{
	long ret = orig_sys_read(regs);

	if (ret > 0 && is_target_file((unsigned int)regs->di))
		redact_buffer((char __user *)regs->si, ret);

	return ret;
}

static void notrace redirect_read(unsigned long ip, unsigned long parent_ip,
				  struct ftrace_ops *ops,
				  struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);

	if (!regs || within_module(parent_ip, THIS_MODULE))
		return;

	regs->ip = (unsigned long)intercepted_read;
}

static int install_hook(void)
{
	int err;

	sys_read_addr = lookup_name("__x64_sys_read");
	if (!sys_read_addr)
		return -ENOENT;

	orig_sys_read = (sys_read_t)(sys_read_addr - ENDBR_SIZE);

	read_ops.func = redirect_read;
	read_ops.flags = FTRACE_OPS_FL_SAVE_REGS |
			 FTRACE_OPS_FL_IPMODIFY |
			 FTRACE_OPS_FL_RECURSION;

	err = ftrace_set_filter_ip(&read_ops, sys_read_addr, 0, 0);
	if (err)
		return err;

	err = register_ftrace_function(&read_ops);
	if (err) {
		ftrace_set_filter_ip(&read_ops, sys_read_addr, 1, 0);
		return err;
	}
	return 0;
}

static void remove_hook(void)
{
	unregister_ftrace_function(&read_ops);
	ftrace_set_filter_ip(&read_ops, sys_read_addr, 1, 0);
}

static int stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "redactions=%d\n", atomic_read(&redact_count));
	return 0;
}

static int __init redactor_init(void)
{
	int err;

	err = install_hook();
	if (err) {
		pr_err("rk: __x64_sys_read not found\n");
		return err;
	}

	proc_entry = proc_create_single("rk_content", 0444, NULL, stats_show);
	if (!proc_entry) {
		remove_hook();
		return -ENOMEM;
	}

	pr_info("rk: content filter active on %s\n", TARGET_NAME);
	return 0;
}

static void __exit redactor_exit(void)
{
	proc_remove(proc_entry);
	remove_hook();
}

module_init(redactor_init);
module_exit(redactor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("File content redaction via sys_read hook");
