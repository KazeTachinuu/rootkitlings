// SPDX-License-Identifier: GPL-2.0
#include "authenticated.h"
#include "platform.h"

static u64 decode_u64(const u8 *input)
{
	return (u64)input[0] << 56 | (u64)input[1] << 48 | (u64)input[2] << 40 |
	       (u64)input[3] << 32 | (u64)input[4] << 24 | (u64)input[5] << 16 |
	       (u64)input[6] << 8 | input[7];
}

static int check_authenticated_fields(const struct rk_receiver *receiver,
				      const u8 *message, u64 *sequence)
{
	u64 presented_epoch;

	if (message[0] != RK_AUTH_VERSION)
		return -EPROTONOSUPPORT;
	if (message[1] != RK_AUTH_COMMAND_SET_LEVEL)
		return -EOPNOTSUPP;
	if (message[18] > RK_AUTH_LEVEL_MAX || message[19] != 0)
		return -EINVAL;
	presented_epoch = decode_u64(message + 2);
	*sequence = decode_u64(message + 10);
	if (presented_epoch != receiver->epoch)
		return -ESTALE;
	if (receiver->exhausted)
		return -EOVERFLOW;
	if (*sequence != receiver->next_sequence)
		return *sequence < receiver->next_sequence ? -EALREADY
							   : -ERANGE;
	return 0;
}

int rk_receiver_start(struct rk_receiver *receiver,
		      const u8 key[RK_AUTH_KEY_LENGTH], size_t key_length,
		      u64 epoch, u64 first_sequence)
{
	void *owned;
	int result;

	if (!receiver || !key || key_length != RK_AUTH_KEY_LENGTH ||
	    epoch == 0 || first_sequence == 0)
		return -EINVAL;
	if (receiver->context || receiver->ready)
		return -EBUSY;
	result = rk_platform_auth_create(&owned, key, key_length);
	if (result)
		return result;
	receiver->context = owned;
	receiver->epoch = epoch;
	receiver->next_sequence = first_sequence;
	receiver->accepted = 0;
	receiver->output_level = 0;
	receiver->exhausted = false;
	receiver->ready = true;
	return 0;
}

void rk_receiver_stop(struct rk_receiver *receiver)
{
	void *owned;

	if (!receiver)
		return;
	owned = receiver->context;
	if (owned)
		rk_platform_auth_destroy(owned);
	rk_platform_clear(receiver, sizeof(*receiver));
}

int rk_receiver_submit(struct rk_receiver *receiver, const u8 *message,
		       size_t message_length)
{
	u8 calculated[RK_AUTH_DIGEST_LENGTH] = {0};
	u64 sequence = 0;
	int result;

	if (!receiver || !message)
		return -EINVAL;
	if (message_length != RK_AUTH_MESSAGE_LENGTH)
		return -EMSGSIZE;
	if (!receiver->context || !receiver->ready)
		return -ENODEV;

	rk_platform_lock(receiver->context);
	result = rk_platform_hmac(receiver->context, message,
				  RK_AUTH_BODY_LENGTH, calculated);
	if (!result &&
	    !rk_platform_tags_equal(calculated, message + RK_AUTH_BODY_LENGTH,
				    RK_AUTH_TAG_LENGTH))
		result = -EBADMSG;
	if (!result)
		result =
		    check_authenticated_fields(receiver, message, &sequence);
	if (!result) {
		receiver->output_level = message[18];
		receiver->accepted += 1;
		if (sequence == ~(u64)0)
			receiver->exhausted = true;
		else
			receiver->next_sequence += 1;
	}
	rk_platform_unlock(receiver->context);
	rk_platform_clear(calculated, sizeof(calculated));
	return result;
}
