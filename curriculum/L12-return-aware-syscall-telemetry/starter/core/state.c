// SPDX-License-Identifier: GPL-2.0
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "rootkit.h"

struct rk_state {
	bool active;
	bool actions_enabled;
	bool selected_success_seen;
	bool selected_failure_seen;
};

__must_check struct rk_state *rk_state_create(void)
{
	struct rk_state *state;

	if (RKLINGS_FAIL_POINT == RK_FAIL_STATE_ALLOC)
		state = NULL;
	else
		state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->active = true;
	state->actions_enabled = false;
	state->selected_success_seen = false;
	state->selected_failure_seen = false;
	return state;
}

bool rk_state_is_active(const struct rk_state *state)
{
	return state->active;
}

bool rk_state_actions_enabled(const struct rk_state *state)
{
	return READ_ONCE(state->actions_enabled);
}

void rk_state_set_actions_enabled(struct rk_state *state, bool enabled)
{
	WRITE_ONCE(state->actions_enabled, enabled);
}

bool rk_state_selected_success_seen(const struct rk_state *state)
{
	return READ_ONCE(state->selected_success_seen);
}

void rk_state_mark_selected_success(struct rk_state *state)
{
	WRITE_ONCE(state->selected_success_seen, true);
}

bool rk_state_selected_failure_seen(const struct rk_state *state)
{
	return READ_ONCE(state->selected_failure_seen);
}

void rk_state_mark_selected_failure(struct rk_state *state)
{
	WRITE_ONCE(state->selected_failure_seen, true);
}

void rk_state_destroy(struct rk_state *state)
{
	if (WARN_ON_ONCE(!state->active))
		return;
	state->active = false;
	kfree(state);
}
