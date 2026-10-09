// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_PREFLIGHT = 1,
	RK_FAIL_STATE_ALLOC = 2,
	RK_FAIL_PROC_PUBLISH = 3,
	RK_FAIL_WORKER_CREATE = 4,
};

struct rk_state;

__must_check struct rk_state *rk_state_create(void);
bool rk_state_is_active(const struct rk_state *state);
bool rk_state_actions_enabled(const struct rk_state *state);
void rk_state_set_actions_enabled(struct rk_state *state, bool enabled);
unsigned long rk_state_worker_runs(const struct rk_state *state);
void rk_state_record_worker_run(struct rk_state *state);
void rk_state_destroy(struct rk_state *state);

__must_check int rk_proc_publish(struct rk_state *state);
void rk_proc_unpublish(void);

__must_check int rk_worker_start(struct rk_state *state);
__must_check int rk_worker_request(void);
void rk_worker_stop(void);

#endif
