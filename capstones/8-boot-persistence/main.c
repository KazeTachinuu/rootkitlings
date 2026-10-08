// SPDX-License-Identifier: GPL-2.0
#include "persistence.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define COMMAND_SIZE 16

static struct proc_dir_entry *control_entry;

static const char *state_name(enum persistence_state state)
{
	switch (state) {
	case PERSISTENCE_ABSENT:
		return "absent";
	case PERSISTENCE_INSTALLED:
		return "installed";
	case PERSISTENCE_FOREIGN:
		return "foreign";
	}
	return "unknown";
}

static int status_show(struct seq_file *output, void *unused)
{
	seq_printf(output, "state=%s\npath=%s\nmodule=hello\n",
		   state_name(persistence_state()), PERSISTENCE_PATH);
	return 0;
}

static int status_open(struct inode *inode, struct file *file)
{
	return single_open(file, status_show, NULL);
}

static ssize_t control_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *position)
{
	char command[COMMAND_SIZE];
	size_t length = count;
	int error;

	if (!count || count >= sizeof(command))
		return -EINVAL;
	if (copy_from_user(command, buffer, count))
		return -EFAULT;
	if (command[length - 1] == '\n')
		length--;
	command[length] = '\0';
	if (!strcmp(command, "install"))
		error = persistence_install();
	else if (!strcmp(command, "remove"))
		error = persistence_remove();
	else
		error = -EINVAL;
	return error ? error : count;
}

static const struct proc_ops control_ops = {
	.proc_open = status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = control_write,
};

static int __init persistence_init(void)
{
	control_entry = proc_create("rk_boot", 0600, NULL, &control_ops);
	return control_entry ? 0 : -ENOMEM;
}

static void __exit persistence_exit(void)
{
	proc_remove(control_entry);
}

module_init(persistence_init);
module_exit(persistence_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Owned modules-load request transaction");
