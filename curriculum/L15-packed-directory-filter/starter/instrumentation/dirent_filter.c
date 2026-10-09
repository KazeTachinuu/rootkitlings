// SPDX-License-Identifier: GPL-2.0
#include "dirent_filter.h"

enum rk_dirent_filter_result
rk_filter_dirents64(void *buffer, size_t length,
		    const char *target, size_t target_length,
		    size_t *output_length)
{
	/* TODO: validate the complete sequence, then compact retained records. */
	*output_length = length;
	return RK_DIRENT_FILTER_INVALID;
}
