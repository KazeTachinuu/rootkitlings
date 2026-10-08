// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/uidgid.h>

static struct proc_dir_entry *entry;

static int info_show(struct seq_file *m, void *v)
{
	const struct cred *cred = current_cred();

	seq_printf(m, "uid=%u gid=%u\n", from_kuid(&init_user_ns, cred->uid),
		   from_kgid(&init_user_ns, cred->gid));
	return 0;
}

static int __init inspect_init(void)
{
	entry = proc_create_single("rk_creds", 0444, NULL, info_show);
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
MODULE_DESCRIPTION("Credential inspection via current_cred");
