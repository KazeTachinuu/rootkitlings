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
	/* TODO: admit a run through the action switch and record it. */
}

static void rk_action_timer(struct timer_list *timer)
{
	/* TODO: record the firing, queue work, and rearm the timer. */
}

void rk_scheduler_start(struct rk_state *state)
{
	/* TODO: initialize the borrowed state, work, and timer, then arm the timer. */
}

void rk_scheduler_stop(void)
{
	/* TODO: shut down the timer, cancel work synchronously, then clear state. */
}
