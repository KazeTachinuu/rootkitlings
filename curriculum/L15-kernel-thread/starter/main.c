// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/wait.h>

static struct task_struct *rk_thread;
static DECLARE_WAIT_QUEUE_HEAD(rk_wq);

// Base delay 5 seconds, jitter up to 3 seconds
static unsigned long base_delay_ms = 5000;
static unsigned long jitter_ms = 3000;

static int rk_agent_fn(void *data)
{
	unsigned long counter = 0;
	unsigned long delay;

	// TODO: Loop while the thread has not been asked to stop.
	// pr_info("rklings: implement the agent loop\n");

	// TODO: Inside the loop, increment counter.

	// TODO: Compute a jittered delay in jiffies:
	//   base_delay_ms + get_random_u32() % jitter_ms
	//   Convert to jiffies with msecs_to_jiffies().

	// TODO: Sleep with wait_event_interruptible_timeout.
	//   Use rk_wq as the wait queue.
	//   Use kthread_should_stop() as the wake condition.

	return 0;
}

static int __init rk_init(void)
{
	// TODO: Start the thread with kthread_run.
	// Name it "rkworker". Check for IS_ERR.
	// pr_info("rklings: start the kernel thread\n");

	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: Stop the thread with kthread_stop.
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
