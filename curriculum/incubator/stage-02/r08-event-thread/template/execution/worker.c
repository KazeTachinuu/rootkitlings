// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "rootkit.h"

#define RK_WORKER_NAME "rkling_event"

static struct rk_state *worker_state;
static struct task_struct *worker_task;
static wait_queue_head_t worker_wait;
static atomic_t pending;

static int rk_worker_main(void *unused)
{
	for (;;) {
		/* TODO: wait for pending work or a stop request. */
		if (kthread_should_stop())
			break;
		/* TODO: consume at most one pending unit and record one run. */
	}

	return 0;
}

__must_check int rk_worker_request(void)
{
	/* TODO: publish one bounded pending unit, then wake the thread. */
	return -EOPNOTSUPP;
}

__must_check int rk_worker_start(struct rk_state *state)
{
	/* TODO: initialize state, wait queue, and count, then create the thread. */
	return -EOPNOTSUPP;
}

void rk_worker_stop(void)
{
	/* TODO: stop and join the task before releasing the borrowed state. */
}
