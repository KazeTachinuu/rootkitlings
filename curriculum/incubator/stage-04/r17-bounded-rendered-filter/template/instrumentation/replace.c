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
	/* TODO: replace complete, non-overlapping, equal-width byte patterns. */
	return 0;
}
