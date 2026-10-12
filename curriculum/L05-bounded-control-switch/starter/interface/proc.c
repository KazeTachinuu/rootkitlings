// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "rootkit.h"

#define RK_COMMAND_MAX (sizeof("disable\n") - 1)

static struct proc_dir_entry *proc_entry;

static int rk_proc_show(struct seq_file *output, void *unused)
{
	const struct rk_state *state = output->private;
	const bool active = rk_state_is_active(state);

	/* TODO: snapshot the action switch and print both exact lines. */
	seq_printf(output, "active=%u\n", active ? 1U : 0U);
	return 0;
}

/* TODO: open one seq_file with the state from pde_data(inode). */

/*
 * TODO: implement the strict write contract. Validate the offset and raw
 * count before copy_from_user(), parse the local bytes, then commit once.
 */

/* TODO: define proc_ops for open, read, write, seek, and release. */

__must_check int rk_proc_publish(struct rk_state *state)
{
	if (proc_entry || !state)
		return -EINVAL;

	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		/* TODO: publish mode 0644 with proc_create_data(). */
		proc_entry = NULL;
	if (!proc_entry)
		return -ENOMEM;

	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
