// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/uidgid.h>

static struct proc_dir_entry *entry;

static ssize_t cred_show(struct file *file, char __user *buffer,
			 size_t count, loff_t *position)
{
	const struct cred *cred = current_cred();
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "uid=%u gid=%u\n",
		       from_kuid(&init_user_ns, cred->uid),
		       from_kgid(&init_user_ns, cred->gid));
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops cred_proc_ops = {
	.proc_read = cred_show,
};

static int __init cred_reader_init(void)
{
	entry = proc_create("rk_creds", 0444, NULL, &cred_proc_ops);
	if (!entry)
		return -ENOMEM;

	pr_info("rk: credential reader ready\n");
	return 0;
}

static void __exit cred_reader_exit(void)
{
	proc_remove(entry);
}

module_init(cred_reader_init);
module_exit(cred_reader_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Credential inspection via current_cred");
