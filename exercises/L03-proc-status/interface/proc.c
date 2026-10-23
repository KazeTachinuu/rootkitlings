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
	// TODO 1: write the stored active state to the proc read buffer
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
	// TODO 2: publish the read-only proc entry for this state
	return -ENOSYS;
}

void rk_proc_unpublish(void)
{
	// TODO 3: remove the proc entry and clear its state reference
}
