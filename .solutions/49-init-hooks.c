// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/umh.h>

#define CMD "touch /tmp/rootkit_was_here"

static struct proc_dir_entry *entry;
static int cmd_result = -1;

static int umh_read(struct seq_file *m, void *v)
{
	seq_printf(m, "cmd=%s\nexit=%d\n",
		   CMD, cmd_result);
	return 0;
}


static int __init umh_init(void)
{
	char *argv[] = { "/bin/sh", "-c", CMD, NULL };
	char *envp[] = { "PATH=/bin:/sbin", NULL };

	entry = proc_create_single("rk_persist", 0444, NULL, umh_read);
	if (!entry)
		return -ENOMEM;

	cmd_result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	pr_info("rk: ran '%s' -> %d\n", CMD, cmd_result);
	return 0;
}

static void __exit umh_exit(void)
{
	proc_remove(entry);
}

module_init(umh_init);
module_exit(umh_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Userspace command execution via call_usermodehelper");
