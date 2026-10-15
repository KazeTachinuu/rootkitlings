// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/cred.h>
#include <linux/sched/task.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/atomic.h>
#include "rklings_hook.h"

#define RK_CMD "rklings.e"
#define RK_CMD_LEN 9
#define RK_PROC_NAME "rklings"

static atomic_t rk_target_pid = ATOMIC_INIT(0);
static struct proc_dir_entry *rk_proc_entry;

/* Escalate the calling process to root. */
static int rk_escalate(void)
{
	/*
	 * TODO: Allocate root credentials with prepare_kernel_cred(&init_task).
	 * Check the return value. Install them with commit_creds.
	 * Return 0 on success, -ENOMEM on failure.
	 */
	return -ENOMEM;
}

RK_ON_MKDIR(name) {
	/*
	 * TODO: Check if "name" starts with "rklings.e".
	 * If it does not match, return -ENOENT.
	 *
	 * If name is exactly "rklings.e" (no suffix):
	 *   Call rk_escalate() for self-escalation.
	 *   Return -ENOENT.
	 *
	 * If name is "rklings.e.<pid>" (dot followed by a number):
	 *   Parse the PID with kstrtoint.
	 *   Store it with atomic_set(&rk_target_pid, pid).
	 *   Return -ENOENT.
	 */
	return -ENOENT;
}

static int rk_proc_show(struct seq_file *m, void *v)
{
	/*
	 * TODO: Read the target PID with atomic_read.
	 * If the target is set and current->pid matches it:
	 *   Call rk_escalate().
	 *   Clear the target with atomic_set to 0.
	 *   Print "rklings: escalated" with seq_printf.
	 *   Return 0.
	 *
	 * Otherwise, print a status line:
	 *   If a target is set: "rklings: waiting for pid <N>"
	 *   If no target: "rklings: idle"
	 */
	seq_printf(m, "rklings: idle\n");
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

static const struct proc_ops rk_proc_ops = {
	.proc_open    = rk_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static int __init rk_init(void)
{
	int ret;

	/*
	 * TODO: Create /proc/rklings with proc_create (mode 0444).
	 * If it fails, return -ENOMEM.
	 *
	 * Start the hook with rk_hook_start().
	 * If it fails, remove the proc entry and return the error.
	 */
	return 0;
}

static void __exit rk_exit(void)
{
	/*
	 * TODO: Stop the hook.
	 * Remove the proc entry.
	 */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings PID-targeted escalation");
