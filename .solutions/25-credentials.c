// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uidgid.h>

static struct proc_dir_entry *entry;

static int cred_show(struct seq_file *m, void *v)
{
	const struct cred *cred = current_cred();

	seq_printf(m, "uid=%u gid=%u\n",
		   from_kuid(&init_user_ns, cred->uid),
		   from_kgid(&init_user_ns, cred->gid));
	return 0;
}

static int __init cred_reader_init(void)
{
	entry = proc_create_single("rk_creds", 0444, NULL, cred_show);
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
