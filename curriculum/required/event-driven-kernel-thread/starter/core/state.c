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
	unsigned long worker_runs;
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
	state->actions_enabled = false;
	state->worker_runs = 0;
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

unsigned long rk_state_worker_runs(const struct rk_state *state)
{
	return READ_ONCE(state->worker_runs);
}

void rk_state_record_worker_run(struct rk_state *state)
{
	unsigned long runs = READ_ONCE(state->worker_runs);

	WRITE_ONCE(state->worker_runs, runs + 1);
}

void rk_state_destroy(struct rk_state *state)
{
	if (WARN_ON_ONCE(!state->active))
		return;
	state->active = false;
	kfree(state);
}
