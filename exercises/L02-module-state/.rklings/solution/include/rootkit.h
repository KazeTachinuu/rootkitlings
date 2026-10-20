// SPDX-License-Identifier: GPL-2.0
#ifndef RKLINGS_ROOTKIT_H
#define RKLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_STATE_ALLOC = 1,
};

struct rk_state;

__must_check struct rk_state *rk_state_create(bool active);
bool rk_state_is_active(const struct rk_state *state);
void rk_state_destroy(struct rk_state *state);

#endif
