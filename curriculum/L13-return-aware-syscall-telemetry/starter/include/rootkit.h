// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

#define RK_FAIL_NONE 0
#define RK_FAIL_PREFLIGHT 1
#define RK_FAIL_STATE_ALLOC 2
#define RK_FAIL_PROC_PUBLISH 3
#define RK_FAIL_KRETPROBE_TARGET 4

#define RK_RETURN_CAPACITY 16

struct rk_state;

__must_check struct rk_state *rk_state_create(void);
bool rk_state_is_active(const struct rk_state *state);
bool rk_state_actions_enabled(const struct rk_state *state);
void rk_state_set_actions_enabled(struct rk_state *state, bool enabled);
bool rk_state_selected_success_seen(const struct rk_state *state);
void rk_state_mark_selected_success(struct rk_state *state);
bool rk_state_selected_failure_seen(const struct rk_state *state);
void rk_state_mark_selected_failure(struct rk_state *state);
void rk_state_destroy(struct rk_state *state);

__must_check int rk_proc_publish(struct rk_state *state);
void rk_proc_unpublish(void);

__must_check int rk_return_probe_start(struct rk_state *state);
void rk_return_probe_stop(void);
unsigned int rk_return_probe_capacity(void);
unsigned int rk_return_probe_missed(void);

#endif
