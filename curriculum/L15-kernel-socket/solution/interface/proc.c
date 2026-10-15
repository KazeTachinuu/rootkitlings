// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;

static int rk_proc_show(struct seq_file *output, void *unused)
{
	struct rk_socket_status status;

	(void)unused;
	rk_socket_snapshot(output->private, &status);
	seq_printf(output,
		   "state=%s\nnamespace=%s\nfamily=%s\ntype=%s\n"
		   "protocol=%s\nconnect_flags=%d\nconnect_result=%d\n"
		   "local_address=%pI4\nlocal_port=%u\n"
		   "peer_address=%pI4\npeer_port=%u\n",
		   status.owned ? "owned" : "empty",
		   status.init_namespace ? "init_net" : "unexpected",
		   status.family == AF_INET ? "ipv4" : "unexpected",
		   status.type == SOCK_STREAM ? "stream" : "unexpected",
		   status.protocol == IPPROTO_TCP ? "tcp" : "unexpected",
		   status.connect_flags, status.connect_result,
		   &status.local_address, status.local_port,
		   &status.peer_address, status.peer_port);
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

__must_check int rk_proc_publish(struct rk_socket_owner *owner)
{
	if (!owner || proc_entry)
		return -EINVAL;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_data("rklings", 0400, NULL,
					      &rk_proc_ops, owner);
	if (!proc_entry)
		return -ENOMEM;
	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
