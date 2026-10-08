// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

static struct task_struct *heartbeat_thread;
static struct proc_dir_entry *entry;
static DECLARE_WAIT_QUEUE_HEAD(heartbeat_wq);
static atomic_t heartbeat_pending = ATOMIC_INIT(0);
static atomic_long_t heartbeat_count = ATOMIC_LONG_INIT(0);

static int heartbeat_loop(void *data)
{
	pr_info("rk: kthread started\n");

	while (!kthread_should_stop()) {
		wait_event_interruptible(heartbeat_wq,
					atomic_read(&heartbeat_pending) ||
					kthread_should_stop());
		if (kthread_should_stop())
			break;

		if (!atomic_xchg(&heartbeat_pending, 0))
			continue;
		atomic_long_inc(&heartbeat_count);
	}

	return 0;
}

static int heartbeat_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%ld\n", atomic_long_read(&heartbeat_count));
	return 0;
}

static int heartbeat_open(struct inode *inode, struct file *file)
{
	return single_open(file, heartbeat_show, NULL);
}

static ssize_t heartbeat_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *position)
{
	char buf[16];

	if (!count)
		return -EINVAL;
	if (count >= sizeof(buf))
		return -E2BIG;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	if (memchr(buf, '\0', count))
		return -EINVAL;
	buf[count] = '\0';
	strim(buf);
	if (!sysfs_streq(buf, "wake"))
		return -EINVAL;

	/* Publish the event, then wake the thread waiting for it. */

	return count;
}

static const struct proc_ops heartbeat_proc_ops = {
	.proc_open    = heartbeat_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = heartbeat_write,
};

static int __init heartbeat_init(void)
{
	entry = proc_create("rk_gate", 0600, NULL, &heartbeat_proc_ops);
	if (!entry)
		return -ENOMEM;

	heartbeat_thread = kthread_create(heartbeat_loop, NULL, "rk_gate");
	if (IS_ERR(heartbeat_thread)) {
		proc_remove(entry);
		return PTR_ERR(heartbeat_thread);
	}

	wake_up_process(heartbeat_thread);
	return 0;
}

static void __exit heartbeat_exit(void)
{
	atomic_set(&heartbeat_pending, 1);
	wake_up_interruptible(&heartbeat_wq);
	kthread_stop(heartbeat_thread);
	proc_remove(entry);
	pr_info("rk: waiter stopped, total wakes=%ld\n",
		atomic_long_read(&heartbeat_count));
}

module_init(heartbeat_init);
module_exit(heartbeat_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Wait queue event signaling");
