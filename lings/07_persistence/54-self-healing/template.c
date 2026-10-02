// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;
static struct task_struct *watchdog;

static int watchdog_fn(void *data)
{
	/* TODO: periodically check if backing file exists, recreate if not */
	return 0;
}

static int watchdog_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int __init watchdog_init(void)
{
	entry = proc_create_single("rk_watchdog", 0444, NULL, watchdog_show);
	if (!entry)
		return -ENOMEM;

	watchdog = kthread_run(watchdog_fn, NULL, "rk_watchdog");
	if (IS_ERR(watchdog)) {
		proc_remove(entry);
		return PTR_ERR(watchdog);
	}

	return 0;
}

static void __exit watchdog_exit(void)
{
	kthread_stop(watchdog);
	proc_remove(entry);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_LICENSE("GPL");
