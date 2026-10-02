// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;
static struct task_struct *worker;

static int thread_fn(void *data)
{
	/* TODO: your kthread loop here */
	return 0;
}

static int thread_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init thread_init(void)
{
	entry = proc_create_single("rk_thread", 0444, NULL, thread_show);
	if (!entry)
		return -ENOMEM;

	worker = kthread_create(thread_fn, NULL, "rk_thread");
	if (IS_ERR(worker)) {
		proc_remove(entry);
		return PTR_ERR(worker);
	}
	wake_up_process(worker);

	return 0;
}

static void __exit thread_exit(void)
{
	kthread_stop(worker);
	proc_remove(entry);
}

module_init(thread_init);
module_exit(thread_exit);

MODULE_LICENSE("GPL");
