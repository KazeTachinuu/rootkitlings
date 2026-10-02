// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

static struct task_struct *waiter_thread;
static struct proc_dir_entry *entry;
static DECLARE_WAIT_QUEUE_HEAD(trigger_wq);
static int triggered;
static unsigned long trigger_count;

static int waiter_loop(void *data)
{
	pr_info("rk: waiter thread started\n");

	while (!kthread_should_stop()) {
		wait_event_interruptible(trigger_wq,
					triggered || kthread_should_stop());
		if (kthread_should_stop())
			break;

		triggered = 0;
		trigger_count++;
		pr_info("rk: woke up, count=%lu\n", trigger_count);
	}

	return 0;
}

static int trigger_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", trigger_count);
	return 0;
}

static int trigger_open(struct inode *inode, struct file *file)
{
	return single_open(file, trigger_show, NULL);
}

static ssize_t trigger_signal(struct file *file, const char __user *buffer,
			      size_t count, loff_t *position)
{
	char buf[16];
	size_t len = count < sizeof(buf) - 1 ? count : sizeof(buf) - 1;

	if (copy_from_user(buf, buffer, len))
		return -EFAULT;
	buf[len] = '\0';

	if (buf[0] == 'w') {
		triggered = 1;
		wake_up_interruptible(&trigger_wq);
	}

	return count;
}

static const struct proc_ops trigger_proc_ops = {
	.proc_open    = trigger_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = trigger_signal,
};

static int __init trigger_init(void)
{
	entry = proc_create("rk_gate", 0666, NULL, &trigger_proc_ops);
	if (!entry)
		return -ENOMEM;

	waiter_thread = kthread_create(waiter_loop, NULL, "rk_gate");
	if (IS_ERR(waiter_thread)) {
		proc_remove(entry);
		return PTR_ERR(waiter_thread);
	}

	wake_up_process(waiter_thread);
	return 0;
}

static void __exit trigger_exit(void)
{
	triggered = 1;
	wake_up_interruptible(&trigger_wq);
	kthread_stop(waiter_thread);
	proc_remove(entry);
	pr_info("rk: waiter stopped, total wakes=%lu\n", trigger_count);
}

module_init(trigger_init);
module_exit(trigger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Wait queue event signaling");
