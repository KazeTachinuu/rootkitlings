// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

static struct work_struct exfil_work;
static struct timer_list schedule_timer;
static struct proc_dir_entry *entry;
static unsigned long exfil_count;

static void exfil_run(struct work_struct *work)
{
	exfil_count++;
	pr_info("rk: work executed, count=%lu\n", exfil_count);
}

static void schedule_exfil(struct timer_list *t)
{
	schedule_work(&exfil_work);
	mod_timer(&schedule_timer, jiffies + HZ);
}

static ssize_t exfil_count_show(struct file *file, char __user *buffer,
				size_t count, loff_t *position)
{
	char buf[32];
	int len;

	len = snprintf(buf, sizeof(buf), "%lu\n", exfil_count);
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops exfil_proc_ops = {
	.proc_read = exfil_count_show,
};

static int __init exfil_init(void)
{
	entry = proc_create("rk_worker", 0444, NULL, &exfil_proc_ops);
	if (!entry)
		return -ENOMEM;

	INIT_WORK(&exfil_work, exfil_run);
	timer_setup(&schedule_timer, schedule_exfil, 0);
	mod_timer(&schedule_timer, jiffies + HZ);

	pr_info("rk: workqueue started\n");
	return 0;
}

static void __exit exfil_exit(void)
{
	timer_shutdown_sync(&schedule_timer);
	cancel_work_sync(&exfil_work);
	proc_remove(entry);
	pr_info("rk: workqueue stopped, final count=%lu\n", exfil_count);
}

module_init(exfil_init);
module_exit(exfil_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Deferred work scheduling");
