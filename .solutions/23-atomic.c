// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define ITERATIONS 10000

static struct proc_dir_entry *entry;
static atomic_t ops_counter = ATOMIC_INIT(0);
static atomic_t active = ATOMIC_INIT(0);
static struct task_struct *racer_a;
static struct task_struct *racer_b;
static atomic_t done_count = ATOMIC_INIT(0);
static DECLARE_COMPLETION(racer_a_done);
static DECLARE_COMPLETION(racer_b_done);

static int atomic_increment(void *data)
{
	int i;

	atomic_inc(&active);
	for (i = 0; i < ITERATIONS; i++)
		atomic_inc(&ops_counter);

	if (atomic_inc_return(&done_count) >= 2)
		pr_info("rk: final count=%d (expected %d)\n",
			atomic_read(&ops_counter), ITERATIONS * 2);
	atomic_dec(&active);
	complete(data);
	return 0;
}

static int counter_show(struct seq_file *m, void *v)
{
	seq_printf(m, "count=%d running=%d\n",
		   atomic_read(&ops_counter), atomic_read(&active));
	return 0;
}

static int __init atomic_init(void)
{
	entry = proc_create_single("rk_atomic", 0444, NULL, counter_show);
	if (!entry)
		return -ENOMEM;

	racer_a = kthread_run(atomic_increment, &racer_a_done, "atomic_a");
	if (IS_ERR(racer_a)) {
		proc_remove(entry);
		return PTR_ERR(racer_a);
	}

	racer_b = kthread_run(atomic_increment, &racer_b_done, "atomic_b");
	if (IS_ERR(racer_b)) {
		wait_for_completion(&racer_a_done);
		proc_remove(entry);
		return PTR_ERR(racer_b);
	}

	pr_info("rk: atomic counter demo\n");
	return 0;
}

static void __exit atomic_exit(void)
{
	wait_for_completion(&racer_b_done);
	wait_for_completion(&racer_a_done);
	proc_remove(entry);
	pr_info("rk: stopped\n");
}

module_init(atomic_init);
module_exit(atomic_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atomic counter operations");
