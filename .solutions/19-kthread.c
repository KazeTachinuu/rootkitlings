// SPDX-License-Identifier: GPL-2.0
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct task_struct *heartbeat_thread;
static struct proc_dir_entry *entry;
static unsigned long heartbeat_count;

static int heartbeat_loop(void *data)
{
	pr_info("rk: kthread started\n");

	while (!kthread_should_stop()) {
		heartbeat_count++;
		ssleep(1);
	}

	pr_info("rk: kthread stopped, count=%lu\n", heartbeat_count);
	return 0;
}

static int heartbeat_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", heartbeat_count);
	return 0;
}

static int __init heartbeat_init(void)
{
	entry = proc_create_single("rk_thread", 0444, NULL, heartbeat_show);
	if (!entry)
		return -ENOMEM;

	heartbeat_thread = kthread_create(heartbeat_loop, NULL, "rk_thread");
	if (IS_ERR(heartbeat_thread)) {
		proc_remove(entry);
		return PTR_ERR(heartbeat_thread);
	}

	wake_up_process(heartbeat_thread);
	return 0;
}

static void __exit heartbeat_exit(void)
{
	kthread_stop(heartbeat_thread);
	proc_remove(entry);
}

module_init(heartbeat_init);
module_exit(heartbeat_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Persistent kernel thread");
