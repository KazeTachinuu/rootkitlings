/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_PROTOCOL_H
#define ROOTKITLINGS_PROTOCOL_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif

#define RK_MARKER_MAX 16

enum rk_command_kind {
	RK_COMMAND_INVALID,
	RK_COMMAND_PING,
	RK_COMMAND_INFO,
	RK_COMMAND_STATUS,
	RK_COMMAND_ENABLE,
	RK_COMMAND_DISABLE,
	RK_COMMAND_RUN,
	RK_COMMAND_MARK,
	RK_COMMAND_QUIT,
};

struct rk_command {
	enum rk_command_kind kind;
	size_t marker_length;
	char marker[RK_MARKER_MAX + 1];
};

int rk_command_parse(const unsigned char *bytes, size_t length,
		     struct rk_command *command);
unsigned int rk_retry_next(unsigned int current_ms, unsigned int base_ms,
			   unsigned int maximum_ms);

#endif
