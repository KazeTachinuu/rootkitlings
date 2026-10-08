// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timer.h>

static struct timer_list beacon_timer;
static struct proc_dir_entry *entry;
static unsigned long beacon_count;

static void beacon_fire(struct timer_list *t)
{
	WRITE_ONCE(beacon_count, READ_ONCE(beacon_count) + 1);
	mod_timer(&beacon_timer, jiffies + HZ);
}

static int beacon_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", READ_ONCE(beacon_count));
	return 0;
}

static int __init beacon_init(void)
{
	entry = proc_create_single("rk_beacon", 0444, NULL, beacon_show);
	if (!entry)
		return -ENOMEM;

	timer_setup(&beacon_timer, beacon_fire, 0);
	mod_timer(&beacon_timer, jiffies + HZ);

	pr_info("rk: timer started (1s interval)\n");
	return 0;
}

static void __exit beacon_exit(void)
{
	timer_shutdown_sync(&beacon_timer);
	proc_remove(entry);
	pr_info("rk: timer stopped, final count=%lu\n",
		READ_ONCE(beacon_count));
}

module_init(beacon_init);
module_exit(beacon_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Periodic timer beacon");
