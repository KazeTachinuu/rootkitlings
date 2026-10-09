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
	if (rk_state_actions_enabled(scheduler_state))
		rk_state_record_worker_run(scheduler_state);
}

static void rk_action_timer(struct timer_list *timer)
{
	rk_state_record_timer_fire(scheduler_state);
	schedule_work(&action_work);
	mod_timer(&action_timer,
		  jiffies + msecs_to_jiffies(RK_ACTION_INTERVAL_MS));
}

void rk_scheduler_start(struct rk_state *state)
{
	scheduler_state = state;
	INIT_WORK(&action_work, rk_action_run);
	timer_setup(&action_timer, rk_action_timer, 0);
	mod_timer(&action_timer,
		  jiffies + msecs_to_jiffies(RK_ACTION_INTERVAL_MS));
}

void rk_scheduler_stop(void)
{
	timer_shutdown_sync(&action_timer);
	cancel_work_sync(&action_work);
	scheduler_state = NULL;
}
