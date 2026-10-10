/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_REPLACE_H
#define ROOTKITLINGS_REPLACE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif

size_t rk_replace_equal(unsigned char *bytes, size_t length,
			const unsigned char *from,
			const unsigned char *to, size_t width);

#endif
