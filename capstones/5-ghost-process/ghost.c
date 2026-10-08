// SPDX-License-Identifier: GPL-2.0
#include <linux/kthread.h>

#include "ghost.h"

static struct task_struct *ghost_task;

static int ghost_loop(void *unused)
{
	while (!kthread_should_stop())
		schedule_timeout_interruptible(HZ);
	return 0;
}

int ghost_start(void)
{
	ghost_task = kthread_run(ghost_loop, NULL, "khidden");
	return PTR_ERR_OR_ZERO(ghost_task);
}

void ghost_stop(void)
{
	kthread_stop(ghost_task);
}

int ghost_pid(void)
{
	return ghost_task->pid;
}
