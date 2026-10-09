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
	const bool actions_enabled = rk_state_actions_enabled(state);
	const long timer_fires = rk_state_timer_fires(state);
	const long actions_completed = rk_state_actions_completed(state);

	seq_printf(output,
		   "active=%u\nactions=%s\ntimer_fires=%ld\nactions_completed=%ld\n",
		   active ? 1U : 0U,
		   actions_enabled ? "enabled" : "disabled", timer_fires,
		   actions_completed);
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, pde_data(inode));
}

static ssize_t rk_control_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *position)
{
	struct rk_state *state = pde_data(file_inode(file));
	char command[RK_COMMAND_MAX];
	size_t length = count;
	bool enable;

	if (*position != 0)
		return -EINVAL;
	if (count > sizeof(command))
		return -E2BIG;
	if (count == 0)
		return -EINVAL;
	if (copy_from_user(command, buffer, count) != 0)
		return -EFAULT;

	if (command[length - 1] == '\n')
		length--;
	if (length == sizeof("enable") - 1 &&
	    memcmp(command, "enable", sizeof("enable") - 1) == 0)
		enable = true;
	else if (length == sizeof("disable") - 1 &&
		 memcmp(command, "disable", sizeof("disable") - 1) == 0)
		enable = false;
	else
		return -EINVAL;

	rk_state_set_actions_enabled(state, enable);
	return count;
}

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_proc_open,
	.proc_read = seq_read,
	.proc_write = rk_control_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

__must_check int rk_proc_publish(struct rk_state *state)
{
	if (proc_entry || !state)
		return -EINVAL;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_data("rootkitlings", 0644, NULL,
					      &rk_proc_ops, state);
	if (!proc_entry)
		return -ENOMEM;

	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
