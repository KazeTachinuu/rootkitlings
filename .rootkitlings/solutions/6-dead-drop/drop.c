// SPDX-License-Identifier: GPL-2.0
#include "dead_drop.h"
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define MESSAGE_SIZE 256

static DEFINE_MUTEX(drop_lock);
static struct proc_dir_entry *drop_entry;
static char message[MESSAGE_SIZE];
static size_t message_length;
static atomic_t drop_unlocked = ATOMIC_INIT(0);

void drop_set_unlocked(bool unlocked)
{
	atomic_set(&drop_unlocked, unlocked);
}

bool drop_is_unlocked(void)
{
	return atomic_read(&drop_unlocked);
}

static int drop_show(struct seq_file *m, void *unused)
{
	int error = 0;

	mutex_lock(&drop_lock);
	if (drop_is_unlocked()) {
		seq_write(m, message, message_length);
		seq_putc(m, '\n');
	} else {
		error = -EACCES;
	}
	mutex_unlock(&drop_lock);
	return error;
}

static int drop_open(struct inode *inode, struct file *file)
{
	return single_open(file, drop_show, NULL);
}

static ssize_t drop_write(struct file *file, const char __user *buffer,
			  size_t count, loff_t *position)
{
	char incoming[MESSAGE_SIZE];
	size_t length = count;
	bool accepted;

	if (!drop_is_unlocked())
		return -EACCES;
	if (!count || count >= sizeof(incoming))
		return -E2BIG;
	if (copy_from_user(incoming, buffer, length))
		return -EFAULT;
	if (incoming[length - 1] == '\n')
		length--;
	if (!length)
		return -EINVAL;
	incoming[length] = '\0';
	mutex_lock(&drop_lock);
	accepted = drop_is_unlocked();
	if (accepted) {
		memcpy(message, incoming, length + 1);
		message_length = length;
	}
	mutex_unlock(&drop_lock);
	return accepted ? count : -EACCES;
}

static const struct proc_ops drop_ops = {
	.proc_open = drop_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = drop_write,
};

int drop_create(void)
{
	drop_entry = proc_create(DROP_NAME, 0600, NULL, &drop_ops);
	return drop_entry ? 0 : -ENOMEM;
}

void drop_destroy(void)
{
	proc_remove(drop_entry);
	drop_entry = NULL;
}
