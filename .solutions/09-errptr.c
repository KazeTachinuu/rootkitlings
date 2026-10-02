// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

static struct proc_dir_entry *dir;
static struct proc_dir_entry *status_entry;

static int status_show(struct seq_file *m, void *v)
{
	seq_puts(m, "ok\n");
	return 0;
}

static void *try_alloc(bool fail)
{
	if (fail)
		return ERR_PTR(-ENOMEM);
	return kmalloc(16, GFP_KERNEL);
}

static int __init errptr_init(void)
{
	void *ptr;

	dir = proc_mkdir("rk_errptr", NULL);
	if (!dir)
		return -ENOMEM;

	status_entry = proc_create_single("status", 0444, dir, status_show);
	if (!status_entry) {
		proc_remove(dir);
		return -ENOMEM;
	}

	ptr = try_alloc(false);
	if (IS_ERR(ptr)) {
		pr_err("rk: errptr demo: error %ld\n", PTR_ERR(ptr));
	} else {
		pr_info("rk: errptr demo: success\n");
		kfree(ptr);
	}

	pr_info("rk: published /proc/rk_errptr/status\n");
	return 0;
}

static void __exit errptr_exit(void)
{
	proc_remove(status_entry);
	proc_remove(dir);
	pr_info("rk: removed /proc/rk_errptr\n");
}

module_init(errptr_init);
module_exit(errptr_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ERR_PTR/IS_ERR kernel error propagation");
