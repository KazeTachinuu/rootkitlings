/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_SELFTEST_H
#define ROOTKITLINGS_SELFTEST_H

#include <linux/stddef.h>

int rk_protection_selftest(unsigned int *assertions, char *runtime_marker,
			   size_t marker_capacity);

#endif
