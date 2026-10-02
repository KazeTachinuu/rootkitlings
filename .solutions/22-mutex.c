// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define ITERATIONS 10000

static struct proc_dir_entry *entry;
static unsigned long shared_counter;
static DEFINE_MUTEX(counter_lock);
static struct task_struct *racer_a;
static struct task_struct *racer_b;
static int done_count;

static int guarded_increment(void *data)
{
	int i;

	for (i = 0; i < ITERATIONS; i++) {
		mutex_lock(&counter_lock);
		shared_counter++;
		mutex_unlock(&counter_lock);
	}

	mutex_lock(&counter_lock);
	done_count++;
	if (done_count >= 2)
		pr_info("rk: final count=%lu (expected %d)\n",
			shared_counter, ITERATIONS * 2);
	mutex_unlock(&counter_lock);
	return 0;
}

static int counter_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", shared_counter);
	return 0;
}

static int __init guarded_init(void)
{
	entry = proc_create_single("rk_mutex", 0444, NULL, counter_show);
	if (!entry)
		return -ENOMEM;

	racer_a = kthread_run(guarded_increment, NULL, "guard_a");
	if (IS_ERR(racer_a)) {
		proc_remove(entry);
		return PTR_ERR(racer_a);
	}

	racer_b = kthread_run(guarded_increment, NULL, "guard_b");
	if (IS_ERR(racer_b)) {
		proc_remove(entry);
		return PTR_ERR(racer_b);
	}

	pr_info("rk: two threads with mutex protection\n");
	return 0;
}

static void __exit guarded_exit(void)
{
	proc_remove(entry);
	pr_info("rk: cleaned up\n");
}

module_init(guarded_init);
module_exit(guarded_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mutex-protected critical sections");
