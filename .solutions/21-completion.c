// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

struct worker {
	struct task_struct *task;
	struct completion done;
};

static struct proc_dir_entry *entry;
static struct worker workers[2];
static atomic_t finished = ATOMIC_INIT(0);

static int worker_fn(void *data)
{
	struct worker *worker = data;

	msleep(100);
	atomic_inc(&finished);
	complete(&worker->done);
	return 0;
}

static int completion_show(struct seq_file *m, void *v)
{
	seq_printf(m, "finished=%d\n", atomic_read(&finished));
	return 0;
}

static int __init completion_init(void)
{
	int i;

	entry = proc_create_single("rk_completion", 0444, NULL,
				   completion_show);
	if (!entry)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		init_completion(&workers[i].done);
		workers[i].task = kthread_run(worker_fn, &workers[i],
					      "rk_worker/%d", i);
		if (IS_ERR(workers[i].task)) {
			int err = PTR_ERR(workers[i].task);

			while (--i >= 0)
				wait_for_completion(&workers[i].done);
			proc_remove(entry);
			return err;
		}
	}
	return 0;
}

static void __exit completion_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(workers); i++)
		wait_for_completion(&workers[i].done);
	proc_remove(entry);
}

module_init(completion_init);
module_exit(completion_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Waiting for finite kernel workers with completions");
