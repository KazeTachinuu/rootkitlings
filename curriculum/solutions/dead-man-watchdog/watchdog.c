// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "watchdog.h"

#define COMMAND_CAPACITY 16
#define WATCHDOG_TIMEOUT (2 * HZ)

static DECLARE_WAIT_QUEUE_HEAD(heartbeat_wait);
static DEFINE_MUTEX(state_lock);
static atomic_t heartbeat_generation = ATOMIC_INIT(0);
static struct proc_dir_entry *watchdog_entry;
static struct task_struct *watchdog_thread;
static unsigned long last_heartbeat;
static unsigned long heartbeats;
static unsigned long timeouts;
static bool stale;

static int watchdog_show(struct seq_file *output, void *unused)
{
	unsigned long age;
	unsigned long heartbeat_count;
	unsigned long timeout_count;
	bool is_stale;

	mutex_lock(&state_lock);
	age = jiffies_to_msecs(jiffies - last_heartbeat);
	heartbeat_count = heartbeats;
	timeout_count = timeouts;
	is_stale = stale;
	mutex_unlock(&state_lock);

	seq_printf(output,
		   "state=%s\nheartbeats=%lu\ntimeouts=%lu\nage_ms=%lu\n"
		   "timeout_ms=%u\n",
		   is_stale ? "stale" : "healthy", heartbeat_count,
		   timeout_count, age, jiffies_to_msecs(WATCHDOG_TIMEOUT));
	return 0;
}

static int watchdog_open(struct inode *inode, struct file *file)
{
	return single_open(file, watchdog_show, NULL);
}

static ssize_t watchdog_write(struct file *file, const char __user *buffer,
			      size_t count, loff_t *position)
{
	char command[COMMAND_CAPACITY];

	if (!count)
		return -EINVAL;
	if (count >= sizeof(command))
		return -E2BIG;
	if (copy_from_user(command, buffer, count))
		return -EFAULT;
	command[count] = '\0';
	if (!sysfs_streq(command, "ping"))
		return -EINVAL;

	mutex_lock(&state_lock);
	last_heartbeat = jiffies;
	heartbeats++;
	stale = false;
	mutex_unlock(&state_lock);
	atomic_inc(&heartbeat_generation);
	wake_up_interruptible(&heartbeat_wait);
	return count;
}

static const struct proc_ops watchdog_ops = {
	.proc_open = watchdog_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = watchdog_write,
};

static int watchdog_loop(void *unused)
{
	int seen_generation = atomic_read(&heartbeat_generation);

	while (!kthread_should_stop()) {
		long remaining;

		remaining = wait_event_interruptible_timeout(
			heartbeat_wait,
			kthread_should_stop() ||
				atomic_read(&heartbeat_generation) != seen_generation,
			WATCHDOG_TIMEOUT);
		if (kthread_should_stop())
			break;
		if (remaining > 0) {
			seen_generation = atomic_read(&heartbeat_generation);
			continue;
		}

		mutex_lock(&state_lock);
		if (!stale &&
		    time_after_eq(jiffies, last_heartbeat + WATCHDOG_TIMEOUT)) {
			stale = true;
			timeouts++;
		}
		mutex_unlock(&state_lock);
	}
	return 0;
}

int watchdog_start(void)
{
	int error;

	mutex_lock(&state_lock);
	last_heartbeat = jiffies;
	heartbeats = 0;
	timeouts = 0;
	stale = false;
	mutex_unlock(&state_lock);

	watchdog_entry = proc_create("rk_watchdog", 0600, NULL, &watchdog_ops);
	if (!watchdog_entry)
		return -ENOMEM;

	watchdog_thread = kthread_run(watchdog_loop, NULL, "rk_watchdog");
	if (IS_ERR(watchdog_thread)) {
		error = PTR_ERR(watchdog_thread);
		watchdog_thread = NULL;
		proc_remove(watchdog_entry);
		watchdog_entry = NULL;
		return error;
	}
	return 0;
}

void watchdog_stop(void)
{
	if (watchdog_thread)
		kthread_stop(watchdog_thread);
	watchdog_thread = NULL;
	proc_remove(watchdog_entry);
	watchdog_entry = NULL;
}
