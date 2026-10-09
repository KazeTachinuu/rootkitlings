// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_DIRENT_FILTER_H
#define ROOTKITLINGS_DIRENT_FILTER_H

#include <linux/types.h>

enum rk_dirent_filter_result {
	RK_DIRENT_FILTER_INVALID,
	RK_DIRENT_FILTER_UNCHANGED,
	RK_DIRENT_FILTER_CHANGED,
};

enum rk_dirent_filter_result
rk_filter_dirents64(void *buffer, size_t length,
		    const char *target, size_t target_length,
		    size_t *output_length);

#endif
