// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "rootkit.h"

enum rk_event_kind {
	RK_EVENT_CREDENTIAL_CHANGE,
	RK_EVENT_RMDIR,
};

struct rk_event {
	u64 sequence;
	pid_t tid;
	enum rk_event_kind kind;
	bool rmdir_success;
};

struct rk_event_snapshot {
	struct rk_event records[RK_EVENT_CAPACITY];
	u64 overwritten;
	unsigned int retained;
};

static DEFINE_SPINLOCK(event_lock);
static struct rk_event event_ring[RK_EVENT_CAPACITY];
static unsigned int oldest;
static unsigned int retained;
static u64 next_sequence;
static u64 overwritten;
static struct proc_dir_entry *events_entry;

void rk_events_reset(void)
{
	spin_lock(&event_lock);
	memset(event_ring, 0, sizeof(event_ring));
	oldest = 0;
	retained = 0;
	next_sequence = 1;
	overwritten = 0;
	spin_unlock(&event_lock);
}

static void rk_events_append(enum rk_event_kind kind, bool rmdir_success)
{
	struct rk_event *record;
	unsigned int index;

	spin_lock(&event_lock);
	if (retained < RK_EVENT_CAPACITY) {
		index = (oldest + retained) % RK_EVENT_CAPACITY;
		retained++;
	} else {
		index = oldest;
		oldest = (oldest + 1U) % RK_EVENT_CAPACITY;
		overwritten++;
	}
	record = &event_ring[index];
	record->sequence = next_sequence++;
	record->tid = task_pid_nr(current);
	record->kind = kind;
	record->rmdir_success = rmdir_success;
	spin_unlock(&event_lock);
}

void rk_events_record_credential_change(void)
{
	rk_events_append(RK_EVENT_CREDENTIAL_CHANGE, false);
}

void rk_events_record_rmdir(long result)
{
	if (result == 0)
		rk_events_append(RK_EVENT_RMDIR, true);
	else if (result < 0)
		rk_events_append(RK_EVENT_RMDIR, false);
}

static void rk_events_snapshot(struct rk_event_snapshot *snapshot)
{
	unsigned int index;

	spin_lock(&event_lock);
	snapshot->retained = retained;
	snapshot->overwritten = overwritten;
	for (index = 0; index < retained; index++)
		snapshot->records[index] =
			event_ring[(oldest + index) % RK_EVENT_CAPACITY];
	spin_unlock(&event_lock);
}

static int rk_events_show(struct seq_file *output, void *unused)
{
	struct rk_event_snapshot snapshot;
	unsigned int index;

	rk_events_snapshot(&snapshot);
	seq_printf(output, "event_capacity=%u\n", RK_EVENT_CAPACITY);
	seq_printf(output, "retained=%u\n", snapshot.retained);
	seq_printf(output, "overwritten=%llu\n",
		   (unsigned long long)snapshot.overwritten);
	seq_printf(output, "return_capacity=%u\n", rk_return_probe_capacity());
	seq_printf(output, "returns_missed=%u\n", rk_return_probe_missed());
	for (index = 0; index < snapshot.retained; index++) {
		const struct rk_event *record = &snapshot.records[index];

		if (record->kind == RK_EVENT_CREDENTIAL_CHANGE) {
			seq_printf(output,
				   "sequence=%llu kind=credential-change tid=%d\n",
				   (unsigned long long)record->sequence,
				   record->tid);
		} else {
			seq_printf(output,
				   "sequence=%llu kind=rmdir tid=%d result=%s\n",
				   (unsigned long long)record->sequence,
				   record->tid,
				   record->rmdir_success ? "success" : "failure");
		}
	}
	return 0;
}

static int rk_events_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_events_show, NULL);
}

static const struct proc_ops rk_events_ops = {
	.proc_open = rk_events_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int rk_events_publish(void)
{
	if (events_entry)
		return -EINVAL;
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_EVENTS_PUBLISH)
		events_entry = NULL;
	else
		events_entry = proc_create("rootkitlings_events", 0400, NULL,
					   &rk_events_ops);
	return events_entry ? 0 : -ENOMEM;
}

void rk_events_unpublish(void)
{
	proc_remove(events_entry);
	events_entry = NULL;
}
