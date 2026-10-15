// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;

static int rk_proc_show(struct seq_file *output, void *unused)
{
	seq_printf(output,
		   "limit=%u\n"
		   "redactions=%lu\n"
		   "oversize_results=%lu\n"
		   "copy_failures=%lu\n"
		   "return_capacity=%u\n"
		   "returns_missed=%u\n",
		   RK_READ_RESULT_LIMIT,
		   rk_read_redactions(),
		   rk_read_oversize_results(),
		   rk_read_copy_failures(),
		   rk_read_return_capacity(),
		   rk_read_returns_missed());
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

__must_check int rk_proc_publish(void)
{
	if (proc_entry)
		return -EINVAL;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create("rklings", 0400, NULL,
					 &rk_proc_ops);
	if (!proc_entry)
		return -ENOMEM;
	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
