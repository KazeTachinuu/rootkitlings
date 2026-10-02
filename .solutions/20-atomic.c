// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

#define ITERATIONS 10000

static struct proc_dir_entry *entry;
static atomic_t ops_counter = ATOMIC_INIT(0);
static atomic_t active = ATOMIC_INIT(0);
static struct task_struct *racer_a;
static struct task_struct *racer_b;
static atomic_t done_count = ATOMIC_INIT(0);

static int atomic_increment(void *data)
{
	int i;

	for (i = 0; i < ITERATIONS; i++)
		atomic_inc(&ops_counter);

	if (atomic_inc_return(&done_count) >= 2)
		pr_info("rk: final count=%d (expected %d)\n",
			atomic_read(&ops_counter), ITERATIONS * 2);
	return 0;
}

static ssize_t counter_show(struct file *file, char __user *buffer,
			    size_t count, loff_t *position)
{
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "count=%d running=%d\n",
		       atomic_read(&ops_counter), atomic_read(&active));
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops atomic_proc_ops = {
	.proc_read = counter_show,
};

static int __init atomic_init(void)
{
	entry = proc_create("rk_atomic", 0444, NULL, &atomic_proc_ops);
	if (!entry)
		return -ENOMEM;

	atomic_set(&active, 1);

	racer_a = kthread_run(atomic_increment, NULL, "atomic_a");
	if (IS_ERR(racer_a)) {
		proc_remove(entry);
		return PTR_ERR(racer_a);
	}

	racer_b = kthread_run(atomic_increment, NULL, "atomic_b");
	if (IS_ERR(racer_b)) {
		proc_remove(entry);
		return PTR_ERR(racer_b);
	}

	pr_info("rk: atomic counter demo\n");
	return 0;
}

static void __exit atomic_exit(void)
{
	atomic_set(&active, 0);
	proc_remove(entry);
	pr_info("rk: stopped\n");
}

module_init(atomic_init);
module_exit(atomic_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atomic counter operations");
