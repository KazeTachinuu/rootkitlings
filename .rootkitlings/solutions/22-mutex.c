// SPDX-License-Identifier: GPL-2.0
#include <linux/completion.h>
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
static DECLARE_COMPLETION(done_a);
static DECLARE_COMPLETION(done_b);

static int racer_fn(void *data)
{
	int i;

	for (i = 0; i < ITERATIONS; i++) {
		mutex_lock(&counter_lock);
		shared_counter++;
		mutex_unlock(&counter_lock);
	}

	complete(data);
	return 0;
}

static int counter_show(struct seq_file *m, void *v)
{
	mutex_lock(&counter_lock);
	seq_printf(m, "%lu\n", shared_counter);
	mutex_unlock(&counter_lock);
	return 0;
}

static int __init racer_init(void)
{
	entry = proc_create_single("rk_mutex", 0444, NULL, counter_show);
	if (!entry)
		return -ENOMEM;

	racer_a = kthread_run(racer_fn, &done_a, "rk_racer/0");
	if (IS_ERR(racer_a)) {
		proc_remove(entry);
		return PTR_ERR(racer_a);
	}

	racer_b = kthread_run(racer_fn, &done_b, "rk_racer/1");
	if (IS_ERR(racer_b)) {
		wait_for_completion(&done_a);
		proc_remove(entry);
		return PTR_ERR(racer_b);
	}

	pr_info("rk: two threads with mutex protection\n");
	return 0;
}

static void __exit racer_exit(void)
{
	wait_for_completion(&done_b);
	wait_for_completion(&done_a);
	proc_remove(entry);
}

module_init(racer_init);
module_exit(racer_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mutex-protected critical sections");
