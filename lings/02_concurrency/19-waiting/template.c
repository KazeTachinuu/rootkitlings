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

static struct proc_dir_entry *entry;
static struct task_struct *worker;
static DECLARE_WAIT_QUEUE_HEAD(wq);

static int thread_fn(void *data)
{
	/* TODO: wait_event loop */
	return 0;
}

static int gate_show(struct seq_file *m, void *v)
{
	/* TODO: use seq_printf(m, ...) or seq_puts(m, ...) */
	return 0;
}

static int gate_open(struct inode *inode, struct file *file)
{
	return single_open(file, gate_show, NULL);
}

static ssize_t gate_write(struct file *file, const char __user *buffer,
			  size_t count, loff_t *position)
{
	/* TODO: wake_up the waitqueue */
	return count;
}

static const struct proc_ops gate_ops = {
	.proc_open    = gate_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = gate_write,
};

static int __init gate_init(void)
{
	entry = proc_create("rk_gate", 0666, NULL, &gate_ops);
	if (!entry)
		return -ENOMEM;

	worker = kthread_create(thread_fn, NULL, "rk_gate");
	if (IS_ERR(worker)) {
		proc_remove(entry);
		return PTR_ERR(worker);
	}
	wake_up_process(worker);

	return 0;
}

static void __exit gate_exit(void)
{
	kthread_stop(worker);
	proc_remove(entry);
}

module_init(gate_init);
module_exit(gate_exit);

MODULE_LICENSE("GPL");
