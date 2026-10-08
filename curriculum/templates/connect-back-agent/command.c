// SPDX-License-Identifier: GPL-2.0
#include "implant.h"
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/umh.h>

#define MARK_PREFIX "/tmp/rk_mark_"
#define LABEL_SIZE  33

static bool valid_label(const char *label)
{
	size_t index;

	if (!*label)
		return false;
	for (index = 0; label[index]; index++) {
		if (index == LABEL_SIZE - 1)
			return false;
		if (!isalnum(label[index]) && label[index] != '-' &&
		    label[index] != '_')
			return false;
	}
	return true;
}

static int create_mark(const char *label, char *reply, size_t size)
{
	char path[sizeof(MARK_PREFIX) + LABEL_SIZE];
	char *argv[] = { "/bin/touch", path, NULL };
	char *envp[] = { "PATH=/bin:/sbin:/usr/bin", NULL };
	int error;

	if (!valid_label(label))
		return scnprintf(reply, size, "ERR invalid label\n");
	snprintf(path, sizeof(path), "%s%s", MARK_PREFIX, label);
	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (error)
		return scnprintf(reply, size, "ERR touch %d\n", error);
	return scnprintf(reply, size, "MARKED %s\n", path);
}

int command_run(const char *command, char *reply, size_t size)
{
	if (!strcmp(command, "PING"))
		return scnprintf(reply, size, "PONG\n");
	if (!strcmp(command, "INFO"))
		return scnprintf(reply, size, "kernel=%s\n",
				 utsname()->release);
	if (!strncmp(command, "MARK ", 5))
		return create_mark(command + 5, reply, size);
	return scnprintf(reply, size, "ERR unknown task\n");
}
