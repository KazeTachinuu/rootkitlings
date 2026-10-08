// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>

enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_PREFLIGHT = 1,
	RK_FAIL_STATE_ALLOC = 2,
};

struct rk_state;

__must_check noinline struct rk_state *rk_state_create(void);
noinline void rk_state_destroy(struct rk_state *state);

#endif
