// SPDX-License-Identifier: GPL-2.0
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "interface.h"
#include "transaction.h"

static struct proc_dir_entry *proc_entry;

static int rk_status_show(struct seq_file *sequence, void *unused)
{
	const struct rk_transaction *transaction = sequence->private;

	(void)unused;
	seq_printf(sequence,
		   "state=%s\noriginal_length=%zu\nintended_length=%u\n"
		   "exchanged=%u\nlast_error=%d\n",
		   rk_transaction_state_name(transaction),
		   transaction->original.length, RK_INTENDED_LENGTH,
		   transaction->exchanged ? 1 : 0, transaction->last_error);
	return 0;
}

static int rk_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_status_show, pde_data(inode));
}

static const struct proc_ops rk_status_ops = {
	.proc_open = rk_status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int rk_proc_init(struct rk_transaction *transaction)
{
	proc_entry = proc_create_data("rootkitlings", 0400, NULL,
				      &rk_status_ops, transaction);
	return proc_entry ? 0 : -ENOMEM;
}

void rk_proc_exit(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
