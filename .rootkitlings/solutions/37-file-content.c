// SPDX-License-Identifier: GPL-2.0
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define TARGET_NAME "rk_target"
#define REDACT_FROM "SECRET"
#define REDACT_TO   "XXXXXX"
#define REDACT_LEN  6

static struct proc_dir_entry *proc_entry;
static atomic_t redact_count = ATOMIC_INIT(0);
static struct inode *target_inode;

struct read_ctx {
	unsigned int fd;
	char __user *ubuf;
};

static bool is_target_file(unsigned int fd)
{
	struct fd f;
	bool match;

	f = fdget(fd);
	if (!fd_file(f))
		return false;

	match = file_inode(fd_file(f)) == target_inode;
	fdput(f);
	return match;
}

static int entry_read(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct read_ctx *ctx = (void *)ri->data;
	struct pt_regs *uregs = (struct pt_regs *)regs->di;

	ctx->fd = (unsigned int)uregs->di;
	ctx->ubuf = (char __user *)uregs->si;
	return 0;
}

static int ret_read(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct read_ctx *ctx = (void *)ri->data;
	long ret = regs_return_value(regs);
	char *kbuf, *p;
	int replacements = 0;

	if (ret <= 0 || !is_target_file(ctx->fd))
		return 0;

	kbuf = kmalloc(ret + 1, GFP_ATOMIC);
	if (!kbuf)
		return 0;

	if (copy_from_user_nofault(kbuf, ctx->ubuf, ret)) {
		kfree(kbuf);
		return 0;
	}
	kbuf[ret] = '\0';

	p = kbuf;
	while ((p = strstr(p, REDACT_FROM)) != NULL) {
		if (p - kbuf + REDACT_LEN > ret)
			break;
		memcpy(p, REDACT_TO, REDACT_LEN);
		replacements++;
		p += REDACT_LEN;
	}

	if (copy_to_user_nofault(ctx->ubuf, kbuf, ret))
		regs_set_return_value(regs, -EFAULT);
	else
		atomic_add(replacements, &redact_count);
	kfree(kbuf);
	return 0;
}

static struct kretprobe krp = {
	.kp.symbol_name = "__x64_sys_read",
	.entry_handler = entry_read,
	.handler = ret_read,
	.data_size = sizeof(struct read_ctx),
};

static int stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "redactions=%d\n", atomic_read(&redact_count));
	return 0;
}

static int __init redactor_init(void)
{
	struct file *target;
	int err;

	target = filp_open("/tmp/" TARGET_NAME, O_RDONLY, 0);
	if (IS_ERR(target))
		return PTR_ERR(target);
	target_inode = igrab(file_inode(target));
	fput(target);
	if (!target_inode)
		return -ENOMEM;

	err = register_kretprobe(&krp);
	if (err) {
		iput(target_inode);
		return err;
	}

	proc_entry = proc_create_single("rk_content", 0444, NULL, stats_show);
	if (!proc_entry) {
		unregister_kretprobe(&krp);
		iput(target_inode);
		return -ENOMEM;
	}

	pr_info("rk: content filter active on %s\n", TARGET_NAME);
	return 0;
}

static void __exit redactor_exit(void)
{
	proc_remove(proc_entry);
	unregister_kretprobe(&krp);
	iput(target_inode);
}

module_init(redactor_init);
module_exit(redactor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("File content redaction via sys_read hook");
