// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/umh.h>

#define PERSIST_PATH "/etc/modules-load.d/hello.conf"

static struct proc_dir_entry *proc_entry;

static int persist_read(struct seq_file *m, void *v)
{
	seq_printf(m, "path=%s\ncontent=hello\n",
		   PERSIST_PATH);
	return 0;
}


static int __init persist_init(void)
{
	struct file *f;
	loff_t off = 0;
	ssize_t written;
	int err;
	char *argv[] = { "/bin/sh", "-c", "mkdir -p /etc/modules-load.d", NULL };
	char *envp[] = { "PATH=/bin:/sbin", NULL };

	err = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (err)
		return err;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	written = kernel_write(f, "hello\n", 6, &off);
	filp_close(f, NULL);
	if (written < 0)
		return written;
	if (written != 6)
		return -EIO;

	proc_entry = proc_create_single("rk_boot", 0444, NULL, persist_read);
	if (!proc_entry) {
		char *rm_argv[] = { "/bin/sh", "-c", "rm -f " PERSIST_PATH,
				    NULL };

		call_usermodehelper(rm_argv[0], rm_argv, envp, UMH_WAIT_PROC);
		return -ENOMEM;
	}

	pr_info("rk: persistence installed at %s\n", PERSIST_PATH);
	return 0;
}

static void __exit persist_exit(void)
{
	char *argv[] = { "/bin/sh", "-c", "rm -f " PERSIST_PATH, NULL };
	char *envp[] = { "PATH=/bin:/sbin", NULL };

	proc_remove(proc_entry);
	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	pr_info("rk: persistence removed\n");
}

module_init(persist_init);
module_exit(persist_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Boot persistence via modules-load.d");
