/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_PROTOCOL_H
#define RKLINGS_PROTOCOL_H

#include <linux/types.h>

enum rk_command_kind {
	RK_COMMAND_INVALID,
	RK_COMMAND_PING,
	RK_COMMAND_INFO,
	RK_COMMAND_STATUS,
	RK_COMMAND_SHELL,
	RK_COMMAND_SHELL_STATUS,
	RK_COMMAND_QUIT,
};

struct rk_command {
	enum rk_command_kind kind;
	u16 shell_port;
};

int rk_command_parse(const unsigned char *bytes, size_t length,
		     struct rk_command *command);
unsigned int rk_retry_next(unsigned int current_ms, unsigned int base_ms,
			   unsigned int maximum_ms);

#endif
