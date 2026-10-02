// SPDX-License-Identifier: GPL-2.0
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#define ITERATIONS 10000

static struct proc_dir_entry *entry;
static unsigned long shared_counter;
static struct task_struct *racer_a;
static struct task_struct *racer_b;
static DECLARE_COMPLETION(start_gate);
static int done_count;

// split read-modify-write to guarantee lost updates on any CPU count
static int race_increment(void *data)
{
	int i;
	unsigned long tmp;

	wait_for_completion(&start_gate);

	for (i = 0; i < ITERATIONS; i++) {
		tmp = shared_counter;
		yield();
		shared_counter = tmp + 1;
	}

	done_count++;
	if (done_count >= 2)
		pr_info("rk: final count=%lu (expected %d)\n",
			shared_counter, ITERATIONS * 2);
	return 0;
}

static int counter_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", shared_counter);
	return 0;
}

static int __init race_init(void)
{
	entry = proc_create_single("rk_race", 0444, NULL, counter_show);
	if (!entry)
		return -ENOMEM;

	racer_a = kthread_run(race_increment, NULL, "racer_a");
	if (IS_ERR(racer_a)) {
		proc_remove(entry);
		return PTR_ERR(racer_a);
	}

	racer_b = kthread_run(race_increment, NULL, "racer_b");
	if (IS_ERR(racer_b)) {
		proc_remove(entry);
		return PTR_ERR(racer_b);
	}

	complete_all(&start_gate);
	pr_info("rk: two threads racing on a shared counter\n");
	return 0;
}

static void __exit race_exit(void)
{
	proc_remove(entry);
	pr_info("rk: cleaned up\n");
}

module_init(race_init);
module_exit(race_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Concurrent shared state access");
