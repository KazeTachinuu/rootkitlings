// SPDX-License-Identifier: GPL-2.0
#include "privilege.h"
#include "trigger.h"
#include <linux/atomic.h>
#include <linux/cred.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#define TOKEN_LENGTH     16
#define COMMAND_CAPACITY (TOKEN_LENGTH + 2)

static atomic_t trigger_count = ATOMIC_INIT(0);
static DEFINE_MUTEX(token_lock);
static struct proc_dir_entry *trigger_entry;
static char active_token[TOKEN_LENGTH + 1];

static void rotate_token(void)
{
	u64 value = get_random_u64();

	snprintf(active_token, sizeof(active_token), "%016llx",
		 (unsigned long long)value);
	pr_info("rk_privesc token=%s\n", active_token);
}

static int trigger_show(struct seq_file *output, void *unused)
{
	seq_printf(output, "triggers=%d\nuid=%u\n",
		   atomic_read(&trigger_count),
		   from_kuid(&init_user_ns, current_uid()));
	return 0;
}

static int trigger_open(struct inode *inode, struct file *file)
{
	return single_open(file, trigger_show, NULL);
}

static ssize_t trigger_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *position)
{
	char command[COMMAND_CAPACITY];
	size_t length = count;
	int error;

	if (!count || count >= sizeof(command))
		return -EINVAL;
	if (copy_from_user(command, buffer, count))
		return -EFAULT;
	if (command[length - 1] == '\n')
		length--;
	command[length] = '\0';
	if (length != TOKEN_LENGTH)
		return -EINVAL;

	mutex_lock(&token_lock);
	if (strcmp(command, active_token)) {
		error = -EACCES;
		goto unlock;
	}
	error = elevate_current();
	if (!error) {
		atomic_inc(&trigger_count);
		rotate_token();
	}
unlock:
	mutex_unlock(&token_lock);
	return error ? error : count;
}

static const struct proc_ops trigger_ops = {
	.proc_open = trigger_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = trigger_write,
};

int trigger_create(void)
{
	trigger_entry = proc_create("rk_privesc", 0666, NULL, &trigger_ops);
	if (!trigger_entry)
		return -ENOMEM;
	atomic_set(&trigger_count, 0);
	rotate_token();
	return 0;
}

void trigger_remove(void)
{
	proc_remove(trigger_entry);
	memzero_explicit(active_token, sizeof(active_token));
}
