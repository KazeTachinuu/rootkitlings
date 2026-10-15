// SPDX-License-Identifier: GPL-2.0
#ifndef RKLINGS_ROOTKIT_H
#define RKLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>

enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_PREFLIGHT = 1,
	RK_FAIL_STATE_ALLOC = 2,
};

/* TODO: declare the private state type and its ownership API. */

#endif
