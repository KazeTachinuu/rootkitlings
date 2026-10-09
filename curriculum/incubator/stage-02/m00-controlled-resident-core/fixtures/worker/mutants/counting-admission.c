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
	for (;;) {
		wait_event_interruptible(worker_wait,
			atomic_read(&pending) != 0 || kthread_should_stop());
		if (kthread_should_stop())
			break;
		if (atomic_xchg(&pending, 0) != 0 &&
		    rk_state_actions_enabled(worker_state))
			rk_state_record_worker_run(worker_state);
	}
	return 0;
}

int rk_worker_request(void)
{
	if (!rk_state_actions_enabled(worker_state))
		return -EACCES;
	atomic_set(&pending, 1);
	wake_up_interruptible(&worker_wait);
	return 0;
}

int rk_worker_start(struct rk_state *state)
{
	worker_state = state;
	init_waitqueue_head(&worker_wait);
	atomic_set(&pending, 0);
	worker_task = ROOTKITLINGS_FAIL_POINT == RK_FAIL_WORKER_CREATE ?
		ERR_PTR(-EINTR) : kthread_run(rk_worker_main, NULL, "rkling_core");
	if (!IS_ERR(worker_task))
		return 0;
	worker_task = NULL;
	worker_state = NULL;
	return -EINTR;
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
