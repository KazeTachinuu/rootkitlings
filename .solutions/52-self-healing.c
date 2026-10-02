// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *entry;
static struct task_struct *watchdog;
static atomic_t heal_count = ATOMIC_INIT(0);
static atomic_t kill_requested = ATOMIC_INIT(0);

static int heal_show(struct seq_file *m, void *v)
{
	seq_printf(m, "heals=%d\nwatchdog=active\n",
		   atomic_read(&heal_count));
	return 0;
}

static int heal_open(struct inode *inode, struct file *file)
{
	return single_open(file, heal_show, NULL);
}

static ssize_t heal_write(struct file *f, const char __user *buf,
			  size_t count, loff_t *pos)
{
	char cmd[8];
	size_t len = count < sizeof(cmd) - 1 ? count : sizeof(cmd) - 1;

	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';
	if (len > 0 && cmd[len - 1] == '\n')
		cmd[len - 1] = '\0';

	// Can't proc_remove from inside a write handler (deadlock).
	// Signal the watchdog to do it.
	if (strcmp(cmd, "kill") == 0)
		atomic_set(&kill_requested, 1);

	return count;
}

static const struct proc_ops heal_proc_ops = {
	.proc_open    = heal_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = heal_write,
};

static int watchdog_fn(void *data)
{
	while (!kthread_should_stop()) {
		if (atomic_cmpxchg(&kill_requested, 1, 0) == 1 && entry) {
			proc_remove(entry);
			entry = NULL;
			pr_info("rk: /proc/rk_watchdog killed\n");
		}

		if (!entry) {
			entry = proc_create("rk_watchdog", 0666, NULL,
					    &heal_proc_ops);
			if (entry) {
				atomic_inc(&heal_count);
				pr_info("rk: self-healed /proc/rk_watchdog\n");
			}
		}
		schedule_timeout_interruptible(HZ / 2);
	}
	return 0;
}

static int __init watchdog_init(void)
{
	entry = proc_create("rk_watchdog", 0666, NULL, &heal_proc_ops);
	if (!entry)
		return -ENOMEM;

	watchdog = kthread_create(watchdog_fn, NULL, "kheal");
	if (IS_ERR(watchdog)) {
		proc_remove(entry);
		return PTR_ERR(watchdog);
	}
	wake_up_process(watchdog);

	pr_info("rk: self-healing watchdog started\n");
	return 0;
}

static void __exit watchdog_exit(void)
{
	kthread_stop(watchdog);
	if (entry)
		proc_remove(entry);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Self-healing procfs watchdog");
