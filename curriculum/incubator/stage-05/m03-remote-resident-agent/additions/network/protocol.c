// SPDX-License-Identifier: GPL-2.0
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#endif

#include "protocol.h"

static bool rk_bytes_equal(const unsigned char *bytes, size_t length,
			   const char *text, size_t text_length)
{
	return length == text_length && !memcmp(bytes, text, text_length);
}

static bool rk_marker_byte(unsigned char byte)
{
	return (byte >= 'a' && byte <= 'z') ||
	       (byte >= 'A' && byte <= 'Z') ||
	       (byte >= '0' && byte <= '9') || byte == '-' || byte == '_';
}

int rk_command_parse(const unsigned char *bytes, size_t length,
		     struct rk_command *command)
{
	struct rk_command parsed = { 0 };
	size_t index;

	if (!bytes || !command)
		return -EINVAL;
	if (rk_bytes_equal(bytes, length, "PING", 4))
		parsed.kind = RK_COMMAND_PING;
	else if (rk_bytes_equal(bytes, length, "INFO", 4))
		parsed.kind = RK_COMMAND_INFO;
	else if (rk_bytes_equal(bytes, length, "QUIT", 4))
		parsed.kind = RK_COMMAND_QUIT;
	else if (length > 5 && length <= 5 + RK_MARKER_MAX &&
		 !memcmp(bytes, "MARK ", 5)) {
		parsed.kind = RK_COMMAND_MARK;
		parsed.marker_length = length - 5;
		for (index = 0; index < parsed.marker_length; index++) {
			if (!rk_marker_byte(bytes[5 + index]))
				return -EINVAL;
			parsed.marker[index] = bytes[5 + index];
		}
		parsed.marker[parsed.marker_length] = '\0';
	} else {
		return -EINVAL;
	}
	*command = parsed;
	return 0;
}

unsigned int rk_retry_next(unsigned int current_ms, unsigned int base_ms,
			   unsigned int maximum_ms)
{
	if (!base_ms || base_ms > maximum_ms)
		return 0;
	if (current_ms < base_ms)
		return base_ms;
	if (current_ms >= maximum_ms || current_ms > maximum_ms / 2)
		return maximum_ms;
	return current_ms * 2;
}
