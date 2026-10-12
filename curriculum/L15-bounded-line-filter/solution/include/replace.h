/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_REPLACE_H
#define RKLINGS_REPLACE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif

size_t rk_remove_lines(unsigned char *buffer, size_t length,
		       const unsigned char *target, size_t target_length);

#endif
