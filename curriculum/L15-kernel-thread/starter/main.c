// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/wait.h>

static struct task_struct *rk_thread;
static DECLARE_WAIT_QUEUE_HEAD(rk_wq);

static unsigned long base_delay_ms = 5000;
static unsigned long jitter_ms = 3000;

static int rk_agent_fn(void *data)
{
	unsigned long counter = 0;
	unsigned long delay;

	// Loop until stopped. Each iteration: increment counter, sleep with jitter.

	return 0;
}

static int __init rk_init(void)
{
	// Start the agent thread.
	return 0;
}

static void __exit rk_exit(void)
{
	// Stop the agent thread.
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
