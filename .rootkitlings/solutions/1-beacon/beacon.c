// SPDX-License-Identifier: GPL-2.0
#include "beacon.h"
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#define DEFAULT_INTERVAL 5
#define MAX_INTERVAL     300

static atomic_long_t ticks = ATOMIC_LONG_INIT(0);
static struct timer_list beacon_timer;
static unsigned long started_at;
static int interval = DEFAULT_INTERVAL;
static bool running;

static void beacon_work_run(struct work_struct *work)
{
	atomic_long_inc(&ticks);
}

static DECLARE_WORK(beacon_work, beacon_work_run);

static void beacon_timer_expired(struct timer_list *timer)
{
	if (!READ_ONCE(running))
		return;
	queue_work(system_wq, &beacon_work);
	mod_timer(&beacon_timer,
		  jiffies + READ_ONCE(interval) * HZ);
}

static int interval_set(const char *text, const struct kernel_param *parameter)
{
	int new_interval;
	int error;

	error = kstrtoint(text, 10, &new_interval);
	if (error)
		return error;
	if (new_interval < 1 || new_interval > MAX_INTERVAL)
		return -EINVAL;
	WRITE_ONCE(*(int *)parameter->arg, new_interval);
	if (READ_ONCE(running))
		mod_timer(&beacon_timer, jiffies + new_interval * HZ);
	return 0;
}

static int interval_get(char *buffer, const struct kernel_param *parameter)
{
	return scnprintf(buffer, PAGE_SIZE, "%d\n",
			 READ_ONCE(*(int *)parameter->arg));
}

static const struct kernel_param_ops interval_ops = {
	.set = interval_set,
	.get = interval_get,
};

module_param_cb(interval, &interval_ops, &interval, 0644);
MODULE_PARM_DESC(interval, "Beacon interval in seconds (1-300)");

int beacon_start(void)
{
	started_at = jiffies;
	atomic_long_set(&ticks, 0);
	timer_setup(&beacon_timer, beacon_timer_expired, 0);
	WRITE_ONCE(running, true);
	mod_timer(&beacon_timer, jiffies + READ_ONCE(interval) * HZ);
	return 0;
}

void beacon_stop(void)
{
	WRITE_ONCE(running, false);
	timer_delete_sync(&beacon_timer);
	cancel_work_sync(&beacon_work);
}

void beacon_snapshot(struct beacon_snapshot *snapshot)
{
	snapshot->uptime = (jiffies - started_at) / HZ;
	snapshot->ticks = atomic_long_read(&ticks);
	snapshot->interval = READ_ONCE(interval);
}
