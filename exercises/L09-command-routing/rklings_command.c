// SPDX-License-Identifier: GPL-2.0
#include "rklings_command.h"
#include "rklings_escalation.h"
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/string.h>

// TODO 1: implement the ping command
static int rk_cmd_ping(const char *arg)
{
	return -ENOSYS;
}

// TODO 2: implement the status command
static int rk_cmd_status(const char *arg)
{
	return -ENOSYS;
}

// TODO 3: preserve parent and explicit-PID escalation
static int rk_cmd_escalate(const char *arg)
{
	return -ENOSYS;
}

typedef int (*rk_handler_t)(const char *arg);

struct rk_cmd_entry {
	const char *name;
	rk_handler_t handler;
};

// TODO 4: map command names to handlers
static const struct rk_cmd_entry rk_commands[] = {
};

// TODO 5: split and dispatch one command
int rk_dispatch(const char *name)
{
	return -ENOSYS;
}
