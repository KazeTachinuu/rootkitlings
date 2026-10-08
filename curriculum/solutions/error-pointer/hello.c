// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

static struct proc_dir_entry *status_entry;
static long simulated_error;
static bool allocation_ok;

static void *try_alloc(bool fail)
{
	if (fail)
		return ERR_PTR(-ENOMEM);
	return kmalloc(16, GFP_KERNEL);
}

static int status_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "error=%ld\n", simulated_error);
	seq_printf(m, "allocation=%s\n", allocation_ok ? "ok" : "failed");
	return 0;
}

static int __init errptr_init(void)
{
	void *ptr;

	ptr = try_alloc(true);
	if (IS_ERR(ptr))
		simulated_error = PTR_ERR(ptr);

	ptr = try_alloc(false);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	if (!ptr)
		return -ENOMEM;
	allocation_ok = true;
	kfree(ptr);

	status_entry = proc_create_single("rk_errptr", 0444, NULL, status_show);
	return status_entry ? 0 : -ENOMEM;
}

static void __exit errptr_exit(void)
{
	proc_remove(status_entry);
}

module_init(errptr_init);
module_exit(errptr_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Decode errors returned through pointers");
