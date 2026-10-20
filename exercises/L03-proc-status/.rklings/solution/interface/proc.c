// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "rootkit.h"

static struct proc_dir_entry *proc_entry;
static struct rk_state *proc_state;

static int rk_status_show(struct seq_file *output, void *unused)
{
	seq_printf(output, "active=%u\n",
		   rk_state_is_active(proc_state) ? 1U : 0U);
	return 0;
}

static struct proc_dir_entry *rk_proc_create(void)
{
	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		return NULL;
	return proc_create_single("rklings", 0444, NULL, rk_status_show);
}

__must_check int rk_proc_publish(struct rk_state *state)
{
	if (proc_entry || !state)
		return -EINVAL;

	proc_state = state;
	proc_entry = rk_proc_create();
	if (!proc_entry)
		goto clear_state;

	return 0;

clear_state:
	proc_state = NULL;
	return -ENOMEM;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
	proc_state = NULL;
	pr_info("rklings: unpublished proc entry\n");
}
