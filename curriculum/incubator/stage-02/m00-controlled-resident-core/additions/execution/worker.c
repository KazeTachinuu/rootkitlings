// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "rootkit.h"

static struct rk_state *worker_state;
static struct task_struct *worker_task;
static wait_queue_head_t worker_wait;
static atomic_t pending;

static int rk_worker_main(void *unused)
{
	/* TODO: wait, prioritize stop, clear one slot, then recheck the gate. */
	return 0;
}

__must_check int rk_worker_request(void)
{
	/* TODO: check the gate and claim the capacity-one slot without blocking. */
	return -EOPNOTSUPP;
}

__must_check int rk_worker_start(struct rk_state *state)
{
	int error;

	worker_state = state;
	init_waitqueue_head(&worker_wait);
	atomic_set(&pending, 0);
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_WORKER_CREATE)
		worker_task = ERR_PTR(-EINTR);
	else
		worker_task = kthread_run(rk_worker_main, NULL, "rkling_core");
	if (!IS_ERR(worker_task))
		return 0;

	error = PTR_ERR(worker_task);
	worker_task = NULL;
	worker_state = NULL;
	return error;
}

void rk_worker_stop(void)
{
	if (!worker_task)
		return;

	kthread_stop(worker_task);
	worker_task = NULL;
	atomic_set(&pending, 0);
	worker_state = NULL;
}
