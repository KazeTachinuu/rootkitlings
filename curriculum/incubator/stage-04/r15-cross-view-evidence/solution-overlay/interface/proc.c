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

static void *rk_proc_start(struct seq_file *output, loff_t *position)
{
	if (*position >= RK_PROBE_COUNT)
		return NULL;
	return (void *)(unsigned long)(*position + 1);
}

static void *rk_proc_next(struct seq_file *output, void *item,
			  loff_t *position)
{
	(*position)++;
	return rk_proc_start(output, position);
}

static void rk_proc_stop(struct seq_file *output, void *item)
{
}

static int rk_proc_show(struct seq_file *output, void *item)
{
	const struct rk_state *state = output->private;
	const enum rk_probe_id id = (unsigned long)item - 1;

	if (id == RK_PROBE_FILENAME_RMDIR)
		return SEQ_SKIP;

	if (id == RK_PROBE_VFS_MKDIR) {
		const bool active = rk_state_is_active(state);
		const bool actions_enabled = rk_state_actions_enabled(state);
		const bool vfs_mkdir_seen = rk_state_vfs_mkdir_seen(state);
		const bool filename_rmdir_seen =
			rk_state_filename_rmdir_seen(state);

		seq_printf(output,
			   "active=%u\nactions=%s\nvfs_mkdir_seen=%u\n"
			   "filename_rmdir_seen=%u\n",
			   active ? 1U : 0U,
			   actions_enabled ? "enabled" : "disabled",
			   vfs_mkdir_seen ? 1U : 0U,
			   filename_rmdir_seen ? 1U : 0U);
	}
	seq_printf(output, "listed_probe=%s\n", rk_probe_name(id));
	return 0;
}

static const struct seq_operations rk_proc_seq_ops = {
	.start = rk_proc_start,
	.next = rk_proc_next,
	.stop = rk_proc_stop,
	.show = rk_proc_show,
};

static int rk_proc_open(struct inode *inode, struct file *file)
{
	struct seq_file *output;
	int error;

	error = seq_open(file, &rk_proc_seq_ops);
	if (error)
		return error;
	output = file->private_data;
	output->private = pde_data(inode);
	return 0;
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
	.proc_release = seq_release,
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
