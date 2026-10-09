// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "rootkit.h"

struct rk_state {
	bool active;
	/* TODO: add an atomic action switch. */
};

__must_check struct rk_state *rk_state_create(void)
{
	struct rk_state *state;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_STATE_ALLOC)
		state = NULL;
	else
		state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->active = true;
	/* TODO: initialize actions as disabled. */
	return state;
}

bool rk_state_is_active(const struct rk_state *state)
{
	return state->active;
}

/* TODO: implement the bool getter and setter with atomic_read()/atomic_set(). */

void rk_state_destroy(struct rk_state *state)
{
	if (WARN_ON_ONCE(!state->active))
		return;
	state->active = false;
	kfree(state);
}
