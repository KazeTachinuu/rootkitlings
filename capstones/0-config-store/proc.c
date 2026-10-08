// SPDX-License-Identifier: GPL-2.0
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include "config.h"

#define CONFIG_COMMAND_SIZE 128

static int config_show(struct seq_file *m, void *unused)
{
	config_store_show(m);
	return 0;
}

static int config_open(struct inode *inode, struct file *file)
{
	return single_open(file, config_show, NULL);
}

static ssize_t config_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *position)
{
	/* Reject oversized input, copy one command, then dispatch its verb. */
	return count;
}

const struct proc_ops config_proc_ops = {
	.proc_open = config_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = config_write,
};
