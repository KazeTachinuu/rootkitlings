// SPDX-License-Identifier: GPL-2.0
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/types.h>
#include "rootkit.h"
#else
#include <stddef.h>
#include <string.h>
#endif

#include "replace.h"

size_t rk_remove_lines(unsigned char *buffer, size_t length,
		       const unsigned char *target, size_t target_length)
{
	/* TODO: remove all lines containing the target string.
	 * Scan the buffer line by line (delimited by '\n').
	 * For each line, check if the target appears anywhere in it.
	 * If it does, skip the line; otherwise keep it by shifting
	 * subsequent bytes backward with memmove.
	 * Return the new (shorter or equal) length. */
	return length;
}
