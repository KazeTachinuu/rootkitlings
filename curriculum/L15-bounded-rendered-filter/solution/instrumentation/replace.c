// SPDX-License-Identifier: GPL-2.0
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/types.h>
#include "rootkit.h"
#else
#include <stddef.h>
#include <string.h>
#endif

size_t rk_replace_equal(unsigned char *bytes, size_t length,
			const unsigned char *from,
			const unsigned char *to, size_t width)
{
	size_t count = 0;
	size_t index = 0;

	if (width == 0 || width > length)
		return 0;

	while (index <= length - width) {
		if (memcmp(bytes + index, from, width) == 0) {
			memcpy(bytes + index, to, width);
			count++;
			index += width;
		} else {
			index++;
		}
	}
	return count;
}
