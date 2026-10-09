// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *evidence_entry;

static int rk_evidence_show(struct seq_file *output, void *unused)
{
	struct pid_namespace *namespace = output->private;
	struct pid *target = rk_getdents_target_pid();
	pid_t target_number = target ? pid_nr_ns(target, namespace) : 0;

	seq_puts(output, "evidence_map_version=1\n");
	seq_puts(output, "map_role=declared-filter-scope\n");
	seq_puts(output, "target_kind=resident-worker-pid\n");
	seq_printf(output, "target_pid=%d\n", target_number);
	seq_puts(output, "filter_gate=actions-enabled-at-iterate-dir-entry\n");
	seq_puts(output, "filter_view=proc-root-getdents64-result\n");
	seq_puts(output, "filter_match=exact-target-pid-name\n");
	seq_puts(output, "filter_nonmatch=retain\n");
	seq_puts(output, "outside_filter_scope=proc-pid-direct-lookup\n");
	seq_puts(output, "outside_filter_scope=proc-pid-comm\n");
	seq_puts(output, "outside_filter_scope=scheduler-task-execution\n");
	seq_puts(output, "outside_filter_scope=proc-modules-content\n");
	seq_puts(output, "outside_filter_scope=sys-module-tree\n");
	seq_puts(output, "outside_filter_scope=non-proc-directories\n");
	seq_puts(output, "filter_kernel_abi=linux-7.2-x86_64\n");
	seq_printf(output, "filter_chunk_capacity=%u\n",
		   RK_DIRECTORY_CHUNK_CAPACITY);
	seq_puts(output, "filter_oversize=leave-unchanged\n");
	seq_puts(output, "filter_all_match=leave-unchanged\n");
	seq_printf(output, "filter_return_capacity=%u\n",
		   RK_DIRECTORY_RETURN_CAPACITY);
	seq_printf(output, "filter_returns_missed=%u\n",
		   rk_getdents_probe_missed());
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
