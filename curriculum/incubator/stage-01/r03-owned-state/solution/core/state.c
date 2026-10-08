// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "rootkit.h"

struct rk_state {
	bool active;
};

__must_check noinline struct rk_state *rk_state_create(void)
{
	struct rk_state *state;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_STATE_ALLOC)
		state = NULL;
	else
		state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->active = true;
	return state;
}

noinline void rk_state_destroy(struct rk_state *state)
{
	state->active = false;
	kfree(state);
}
