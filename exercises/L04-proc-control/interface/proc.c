// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;
static struct rk_state *proc_state;

static int rk_status_show(struct seq_file *output, void *unused)
{
	seq_printf(output, "active=%u\n",
		   rk_state_is_active(proc_state) ? 1U : 0U);
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_status_show, NULL);
}

static ssize_t rk_proc_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	// TODO 1: copy one command byte and update active state
	return -ENOSYS;
}

static const struct proc_ops rk_proc_ops = {
    .proc_open = rk_proc_open,
    .proc_read = seq_read,
    .proc_write = rk_proc_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static struct proc_dir_entry *rk_proc_create(void)
{
	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		return NULL;
	return proc_create("rklings", 0644, NULL, &rk_proc_ops);
}

__must_check int rk_proc_publish(struct rk_state *state)
{
	if (proc_entry || !state)
		return -EINVAL;

	proc_state = state;

	proc_entry = rk_proc_create();
	if (!proc_entry) {
		proc_state = NULL;
		return -ENOMEM;
	}

	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
	proc_state = NULL;
	pr_info("rklings: unpublished proc entry\n");
}
