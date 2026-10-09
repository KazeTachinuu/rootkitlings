// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "rootkit.h"

struct rk_state {
	bool active;
	atomic_t actions_enabled;
	/* TODO: add atomic_long_t counters for timer fires and completed actions. */
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
	atomic_set(&state->actions_enabled, 0);
	/* TODO: initialize both counters. */
	return state;
}

bool rk_state_is_active(const struct rk_state *state)
{
	return state->active;
}

bool rk_state_actions_enabled(const struct rk_state *state)
{
	return atomic_read(&state->actions_enabled) != 0;
}

void rk_state_set_actions_enabled(struct rk_state *state, bool enabled)
{
	atomic_set(&state->actions_enabled, enabled ? 1 : 0);
}

/* TODO: implement one read and one increment operation for each counter. */

void rk_state_destroy(struct rk_state *state)
{
	if (WARN_ON_ONCE(!state->active))
		return;
	state->active = false;
	kfree(state);
}
