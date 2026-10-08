// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/kprobes.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#include "tripwire.h"

#define EVENT_CAPACITY 16
#define PATH_CAPACITY 64

struct tripwire_event {
	unsigned long sequence;
	unsigned int uid;
	pid_t pid;
	char path[PATH_CAPACITY];
};

static bool armed = true;
module_param(armed, bool, 0644);
MODULE_PARM_DESC(armed, "Enable file access logging");

static DEFINE_SPINLOCK(events_lock);
static struct tripwire_event events[EVENT_CAPACITY];
static unsigned int event_count;
static unsigned int event_head;
static unsigned long next_sequence = 1;

static bool watched_path(const char *path)
{
	return !strcmp(path, "/etc/passwd") || !strcmp(path, "/etc/shadow");
}

static long copy_user_path(char *destination, const char __user *source)
{
	size_t index;

	for (index = 0; index < PATH_CAPACITY; index++) {
		if (copy_from_user_nofault(destination + index, source + index, 1))
			return -EFAULT;
		if (!destination[index])
			return index;
	}
	return -ENAMETOOLONG;
}

static void record_event(const char *path)
{
	struct tripwire_event *event;
	unsigned int index;

	spin_lock(&events_lock);
	index = (event_head + event_count) % EVENT_CAPACITY;
	if (event_count == EVENT_CAPACITY) {
		event_head = (event_head + 1) % EVENT_CAPACITY;
	} else {
		event_count++;
	}

	event = &events[index];
	event->sequence = next_sequence++;
	event->uid = from_kuid_munged(&init_user_ns, current_uid());
	event->pid = current->pid;
	strscpy(event->path, path, sizeof(event->path));
	spin_unlock(&events_lock);
}

static int capture_open(struct kprobe *probe, struct pt_regs *registers)
{
	const char __user *user_path = (const char __user *)registers->si;
	char path[PATH_CAPACITY];
	long length;

	if (!READ_ONCE(armed) || !user_path)
		return 0;

	length = copy_user_path(path, user_path);
	if (length < 0)
		return 0;
	if (watched_path(path))
		record_event(path);
	return 0;
}

static struct kprobe open_probe = {
	.symbol_name = "do_sys_openat2",
	.pre_handler = capture_open,
};

int tripwire_events_show(struct seq_file *output, void *unused)
{
	struct tripwire_event snapshot[EVENT_CAPACITY];
	unsigned int count;
	unsigned int index;

	spin_lock(&events_lock);
	count = event_count;
	for (index = 0; index < count; index++)
		snapshot[index] =
			events[(event_head + index) % EVENT_CAPACITY];
	spin_unlock(&events_lock);

	for (index = 0; index < count; index++)
		seq_printf(output, "event=%lu uid=%u pid=%d path=%s\n",
			   snapshot[index].sequence, snapshot[index].uid,
			   snapshot[index].pid, snapshot[index].path);
	return 0;
}

int tripwire_monitor_start(void)
{
	event_count = 0;
	event_head = 0;
	next_sequence = 1;
	return register_kprobe(&open_probe);
}

void tripwire_monitor_stop(void)
{
	unregister_kprobe(&open_probe);
	event_count = 0;
	event_head = 0;
}
