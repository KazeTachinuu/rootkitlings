// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *hidden_entry;

static int rk_hidden_show(struct seq_file *output, void *unused)
{
	seq_puts(output, "present=1\n");
	return 0;
}

static int rk_hidden_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_hidden_show, NULL);
}

static const struct proc_ops rk_hidden_ops = {
	.proc_open = rk_hidden_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

__must_check int rk_hidden_proc_publish(void)
{
	if (hidden_entry)
		return -EINVAL;
	if (RKLINGS_FAIL_POINT == RK_FAIL_HIDDEN_PUBLISH)
		hidden_entry = NULL;
	else
		hidden_entry = proc_create(RK_DIRECTORY_TARGET, 0400, NULL,
					   &rk_hidden_ops);
	if (!hidden_entry)
		return -ENOMEM;
	return 0;
}

void rk_hidden_proc_unpublish(void)
{
	proc_remove(hidden_entry);
	hidden_entry = NULL;
}
