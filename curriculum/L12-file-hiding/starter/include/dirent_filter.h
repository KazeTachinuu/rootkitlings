// SPDX-License-Identifier: GPL-2.0
#ifndef RKLINGS_DIRENT_FILTER_H
#define RKLINGS_DIRENT_FILTER_H

#include <linux/types.h>

enum rk_dirent_filter_result {
	RK_DIRENT_FILTER_INVALID,
	RK_DIRENT_FILTER_UNCHANGED,
	RK_DIRENT_FILTER_CHANGED,
};

/* Filtering may call the matcher twice for one valid record. */
typedef bool (*rk_dirent_matcher_t)(const char *name, size_t length,
				    const void *context);

enum rk_dirent_filter_result
rk_filter_dirents64_if(void *buffer, size_t length,
		       rk_dirent_matcher_t matches, const void *context,
		       size_t *output_length);

enum rk_dirent_filter_result
rk_filter_dirents64(void *buffer, size_t length,
		    const char *target, size_t target_length,
		    size_t *output_length);

#endif
