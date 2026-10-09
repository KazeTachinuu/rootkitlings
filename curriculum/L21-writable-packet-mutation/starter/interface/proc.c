// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;

static int rk_proc_show(struct seq_file *output, void *unused)
{
	struct rk_netfilter_status status;

	(void)unused;
	rk_netfilter_snapshot(output->private, &status);
	seq_printf(output,
		   "state=%s\nmatch_port=%u\nreplacement_port=%u\n"
		   "callbacks=%llu\nrewritten=%llu\nunchanged=%llu\n"
		   "invalid=%llu\nunsupported=%llu\nstale=%llu\n"
		   "not_writable=%llu\n",
		   status.registered ? "registered" : "unregistered",
		   status.match_port, status.replacement_port, status.callbacks,
		   status.rewritten, status.unchanged, status.invalid,
		   status.unsupported, status.stale, status.not_writable);
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, pde_data(inode));
}

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

__must_check int rk_proc_publish(struct rk_netfilter_mutator *mutator)
{
	if (proc_entry || !mutator)
		return -EINVAL;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_data("rootkitlings", 0400, NULL,
					      &rk_proc_ops, mutator);
	if (!proc_entry)
		return -ENOMEM;
	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
