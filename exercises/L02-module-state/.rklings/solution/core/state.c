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

struct rk_state {
	bool active;
};

__must_check struct rk_state *rk_state_create(bool active)
{
	struct rk_state *state;

	state = rk_state_alloc(sizeof(*state));
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->active = active;
	return state;
}

bool rk_state_is_active(const struct rk_state *state)
{
	return state->active;
}

void rk_state_destroy(struct rk_state *state)
{
	kfree(state);
	pr_info("rklings: released state\n");
}
