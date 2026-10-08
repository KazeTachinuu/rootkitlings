// SPDX-License-Identifier: GPL-2.0
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "config.h"

#define CONFIG_COMMAND_SIZE 128
#define SET_PREFIX	    "set:"
#define DELETE_PREFIX	    "del:"

static int config_show(struct seq_file *m, void *unused)
{
	config_store_show(m);
	return 0;
}

static int config_open(struct inode *inode, struct file *file)
{
	return single_open(file, config_show, NULL);
}

static int dispatch_command(char *command)
{
	char *value;

	if (str_has_prefix(command, SET_PREFIX)) {
		command += strlen(SET_PREFIX);
		value = strchr(command, '=');
		if (!value)
			return -EINVAL;
		*value++ = '\0';
		return config_store_set(command, value);
	}
	if (str_has_prefix(command, DELETE_PREFIX)) {
		config_store_delete(command + strlen(DELETE_PREFIX));
		return 0;
	}
	return -EINVAL;
}

static ssize_t config_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *position)
{
	char command[CONFIG_COMMAND_SIZE];
	size_t length;
	int error;

	if (!count || count >= sizeof(command))
		return -E2BIG;
	length = count;
	if (copy_from_user(command, buffer, length))
		return -EFAULT;
	command[length] = '\0';
	strim(command);
	error = dispatch_command(command);
	return error ? error : count;
}

const struct proc_ops config_proc_ops = {
	.proc_open = config_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = config_write,
};
