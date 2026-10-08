// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

static struct work_struct beacon_work;
static struct timer_list beacon_timer;
static struct proc_dir_entry *entry;
static atomic_long_t beacon_count = ATOMIC_LONG_INIT(0);

static void beacon_run(struct work_struct *work)
{
	atomic_long_inc(&beacon_count);
}

static void beacon_fire(struct timer_list *t)
{
	schedule_work(&beacon_work);
	mod_timer(&beacon_timer, jiffies + HZ);
}

static int beacon_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%ld\n", atomic_long_read(&beacon_count));
	return 0;
}

static int __init beacon_init(void)
{
	entry = proc_create_single("rk_worker", 0444, NULL, beacon_show);
	if (!entry)
		return -ENOMEM;

	INIT_WORK(&beacon_work, beacon_run);
	timer_setup(&beacon_timer, beacon_fire, 0);
	mod_timer(&beacon_timer, jiffies + HZ);

	pr_info("rk: workqueue started\n");
	return 0;
}

static void __exit beacon_exit(void)
{
	timer_shutdown_sync(&beacon_timer);
	cancel_work_sync(&beacon_work);
	proc_remove(entry);
	pr_info("rk: workqueue stopped, final count=%ld\n",
		atomic_long_read(&beacon_count));
}

module_init(beacon_init);
module_exit(beacon_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Deferred work scheduling");
