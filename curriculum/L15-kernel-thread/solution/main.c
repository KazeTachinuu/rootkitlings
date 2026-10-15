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
	unsigned long delay;

	while (!kthread_should_stop()) {

		delay = msecs_to_jiffies(base_delay_ms +
					 get_random_u32() % jitter_ms);
		wait_event_interruptible_timeout(rk_wq,
						 kthread_should_stop(),
						 delay);
	}

	return 0;
}

static int __init rk_init(void)
{
	rk_thread = kthread_run(rk_agent_fn, NULL, "rkworker");
	if (IS_ERR(rk_thread))
		return PTR_ERR(rk_thread);

	return 0;
}

static void __exit rk_exit(void)
{
	kthread_stop(rk_thread);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel thread with jittered backoff");
