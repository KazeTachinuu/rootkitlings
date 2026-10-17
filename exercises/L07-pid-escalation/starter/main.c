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

static int rk_escalate(void)
{
	// TODO: rk_escalate (prepare_kernel_cred + commit_creds)
	return -ENOMEM;
}

RK_ON_MKDIR(name)
{
	// TODO: three cases (self-escalate, store PID, ignore)
	return -ENOENT;
}

static int rk_proc_show(struct seq_file *m, void *v)
{
	// TODO: check current->pid against target, escalate on match, print status
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

	// TODO: create /proc/rklings, start hook (rollback on failure)
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: tear down hook and proc entry
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings PID-targeted escalation");
