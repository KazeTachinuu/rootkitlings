// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/netfilter.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;

static const char *rk_family_name(u8 family)
{
	return family == NFPROTO_IPV4 ? "ipv4" : "unexpected";
}

static const char *rk_hook_name(unsigned int hook)
{
	return hook == NF_INET_LOCAL_OUT ? "local-out" : "unexpected";
}

static int rk_proc_show(struct seq_file *output, void *unused)
{
	struct rk_netfilter_status status;

	(void)unused;
	rk_netfilter_snapshot(output->private, &status);
	seq_printf(output,
		   "state=%s\nfamily=%s\nhook=%s\npriority=%d\n"
		   "namespace=%s\nverdict=accept\ncallbacks=%llu\n",
		   status.registered ? "registered" : "unregistered",
		   rk_family_name(status.family), rk_hook_name(status.hook),
		   status.priority,
		   status.init_namespace ? "init_net" : "unexpected",
		   status.callbacks);
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

__must_check int rk_proc_publish(struct rk_netfilter_observer *observer)
{
	if (proc_entry || !observer)
		return -EINVAL;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_data("rklings", 0400, NULL,
					      &rk_proc_ops, observer);
	if (!proc_entry)
		return -ENOMEM;
	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
