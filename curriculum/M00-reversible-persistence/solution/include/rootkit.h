// SPDX-License-Identifier: GPL-2.0
#ifndef RKLINGS_ROOTKIT_H
#define RKLINGS_ROOTKIT_H

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
struct rk_transaction;

__must_check struct rk_state *rk_state_create(void);
bool rk_state_is_active(const struct rk_state *state);
bool rk_state_actions_enabled(const struct rk_state *state);
void rk_state_set_actions_enabled(struct rk_state *state, bool enabled);
unsigned long rk_state_timer_fires(const struct rk_state *state);
void rk_state_record_timer_fire(struct rk_state *state);
unsigned long rk_state_worker_runs(const struct rk_state *state);
void rk_state_record_worker_run(struct rk_state *state);
bool rk_state_persist_requested(const struct rk_state *state);
void rk_state_set_persist_requested(struct rk_state *state, bool requested);
void rk_state_destroy(struct rk_state *state);

__must_check int rk_proc_publish(struct rk_state *state,
				 struct rk_transaction *transaction);
void rk_proc_unpublish(void);

__must_check int rk_worker_start(struct rk_state *state,
				 struct rk_transaction *transaction);
__must_check int rk_worker_request(void);
void rk_worker_stop(void);

#endif
