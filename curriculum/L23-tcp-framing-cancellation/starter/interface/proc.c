// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;

static int rk_proc_show(struct seq_file *output, void *unused)
{
	struct rk_session_status status;

	(void)unused;
	rk_session_snapshot(output->private, &status);
	seq_printf(output,
		   "state=%s\nsocket=%s\nstopping=%u\npeer_address=127.0.0.1\n"
		   "peer_port=%u\nframes=%llu\noversized=%llu\nlast_error=%d\n",
		   status.running ? "running" : "stopped",
		   status.active ? "active" : "none",
		   status.stopping ? 1U : 0U, status.peer_port,
		   status.frames, status.oversized, status.last_error);
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

__must_check int rk_proc_publish(struct rk_session *session)
{
	if (!session || proc_entry)
		return -EINVAL;
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_data("rootkitlings", 0400, NULL,
					      &rk_proc_ops, session);
	return proc_entry ? 0 : -ENOMEM;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
