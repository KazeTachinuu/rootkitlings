/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_REPLACE_H
#define RKLINGS_REPLACE_H

#include <linux/types.h>

size_t rk_remove_lines(unsigned char *buffer, size_t length,
		       const unsigned char *target, size_t target_length);

#endif
