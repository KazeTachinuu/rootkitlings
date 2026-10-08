// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define ITERATIONS 10000

static struct proc_dir_entry *entry;
static atomic_t shared_counter = ATOMIC_INIT(0);
static atomic_t running = ATOMIC_INIT(0);
static atomic_t finished = ATOMIC_INIT(0);
static struct task_struct *racer_a;
static struct task_struct *racer_b;

static int racer_fn(void *unused)
{
	int i;

	atomic_inc(&running);
	for (i = 0; i < ITERATIONS; i++)
		atomic_inc(&shared_counter);

	atomic_dec(&running);
	atomic_inc(&finished);
	return 0;
}

static int counter_show(struct seq_file *m, void *v)
{
	seq_printf(m, "count=%d running=%d finished=%d\n",
		   atomic_read(&shared_counter), atomic_read(&running),
		   atomic_read(&finished));
	return 0;
}

static int __init racer_init(void)
{
	entry = proc_create_single("rk_atomic", 0444, NULL, counter_show);
	if (!entry)
		return -ENOMEM;

	racer_a = kthread_run(racer_fn, NULL, "rk_racer/0");
	if (IS_ERR(racer_a)) {
		proc_remove(entry);
		return PTR_ERR(racer_a);
	}

	racer_b = kthread_run(racer_fn, NULL, "rk_racer/1");
	if (IS_ERR(racer_b)) {
		kthread_stop(racer_a);
		proc_remove(entry);
		return PTR_ERR(racer_b);
	}

	pr_info("rk: atomic counter demo\n");
	return 0;
}

static void __exit racer_exit(void)
{
	kthread_stop(racer_b);
	kthread_stop(racer_a);
	proc_remove(entry);
}

module_init(racer_init);
module_exit(racer_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atomic counter operations");
