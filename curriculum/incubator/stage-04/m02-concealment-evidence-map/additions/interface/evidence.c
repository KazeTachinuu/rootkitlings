// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *evidence_entry;

static int rk_evidence_show(struct seq_file *output, void *unused)
{
	/* TODO: emit the exact declared-scope record in the reader's PID namespace. */
	return 0;
}

static int rk_evidence_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_evidence_show,
			   proc_pid_ns(file_inode(file)->i_sb));
}

static const struct proc_ops rk_evidence_ops = {
	.proc_open = rk_evidence_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int rk_evidence_publish(void)
{
	/* TODO: publish rootkitlings_evidence with the specified mode and failure. */
	return -ENOMEM;
}

void rk_evidence_unpublish(void)
{
	proc_remove(evidence_entry);
	evidence_entry = NULL;
}
