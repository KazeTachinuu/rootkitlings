// SPDX-License-Identifier: GPL-2.0
#include <linux/jiffies.h>
#include <linux/timer.h>

#include "rootkit.h"

#define RK_ACTION_INTERVAL_MS 100

static struct rk_state *scheduler_state;
static struct timer_list action_timer;

static void rk_action_timer(struct timer_list *timer)
{
	/* TODO: record, request through the shared path, and rearm. */
}

void rk_scheduler_start(struct rk_state *state)
{
	scheduler_state = state;
	timer_setup(&action_timer, rk_action_timer, 0);
	mod_timer(&action_timer,
		  jiffies + msecs_to_jiffies(RK_ACTION_INTERVAL_MS));
}

void rk_scheduler_stop(void)
{
	timer_shutdown_sync(&action_timer);
	scheduler_state = NULL;
}
