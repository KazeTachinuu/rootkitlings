// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include "rootkit.h"

static void *rk_state_alloc(size_t size)
{
	if (RKLINGS_FAIL_POINT == RK_FAIL_STATE_ALLOC)
		return NULL;
	return kzalloc(size, GFP_KERNEL);
}

// TODO 1: define the private active field

__must_check struct rk_state *rk_state_create(bool active)
{
	// TODO 2: allocate state and remember whether it is active
	return ERR_PTR(-ENOMEM);
}

bool rk_state_is_active(const struct rk_state *state)
{
	// TODO 3: return whether the state is active
	return false;
}

void rk_state_destroy(struct rk_state *state)
{
	// TODO 4: free the state and report its release
}
