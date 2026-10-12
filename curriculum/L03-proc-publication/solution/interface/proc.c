// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;

static int rk_status_show(struct seq_file *output, void *unused)
{
	seq_puts(output, "active\n");
	return 0;
}

__must_check int rk_proc_publish(void)
{
	if (proc_entry)
		return -EINVAL;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_single("rklings", 0444, NULL,
						rk_status_show);
	if (!proc_entry)
		return -ENOMEM;

	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
