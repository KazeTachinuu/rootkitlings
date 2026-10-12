// SPDX-License-Identifier: GPL-2.0
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "rootkit.h"

#define RK_ACTION_INTERVAL_MS 100

static struct rk_state *scheduler_state;
static struct timer_list action_timer;
static struct work_struct action_work;

static void rk_action_run(struct work_struct *work)
{
	/* TODO: execute a guarded action cycle. */
}

static void rk_action_timer(struct timer_list *timer)
{
	/* TODO: bridge the timer into the work queue and keep it running. */
}

void rk_scheduler_start(struct rk_state *state)
{
	/* TODO: set up and start the periodic execution engine. */
}

void rk_scheduler_stop(void)
{
	/* TODO: tear down the execution engine cleanly. */
}
