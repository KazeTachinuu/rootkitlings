// SPDX-License-Identifier: GPL-2.0
#include "authenticated.h"
#include "auth_platform.h"

static u64 rk_read_be64(const u8 *bytes)
{
	u64 value = 0;
	size_t index;

	for (index = 0; index < 8; index++)
		value = (value << 8) | bytes[index];
	return value;
}

int rk_receiver_start(struct rk_receiver *receiver,
		      const u8 key[RK_AUTH_KEY_LENGTH], size_t key_length,
		      u64 epoch, u64 first_sequence)
{
	void *context = NULL;
	int error;

	if (!receiver || !key || key_length != RK_AUTH_KEY_LENGTH || !epoch ||
	    !first_sequence)
		return -EINVAL;
	if (receiver->ready || receiver->context)
		return -EBUSY;
	error = rk_platform_auth_create(&context, key, key_length);
	if (error)
		return error;
	receiver->context = context;
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
	if (!receiver)
		return;
	if (receiver->context)
		rk_platform_auth_destroy(receiver->context);
	rk_platform_clear(receiver, sizeof(*receiver));
}

int rk_receiver_submit(struct rk_receiver *receiver, const u8 *message,
		       size_t message_length)
{
	u8 digest[RK_AUTH_DIGEST_LENGTH] = {0};
	u64 epoch;
	u64 sequence;
	int error;

	if (!receiver || !message)
		return -EINVAL;
	if (message_length != RK_AUTH_MESSAGE_LENGTH)
		return -EMSGSIZE;
	if (!receiver->ready || !receiver->context)
		return -ENODEV;

	rk_platform_lock(receiver->context);
	error = rk_platform_hmac(receiver->context, message,
				 RK_AUTH_BODY_LENGTH, digest);
	if (error)
		goto out;
	if (!rk_platform_tags_equal(digest, message + RK_AUTH_BODY_LENGTH,
				    RK_AUTH_TAG_LENGTH)) {
		error = -EBADMSG;
		goto out;
	}
	if (message[0] != RK_AUTH_VERSION) {
		error = -EPROTONOSUPPORT;
		goto out;
	}
	if (message[1] != RK_AUTH_COMMAND_SET_LEVEL) {
		error = -EOPNOTSUPP;
		goto out;
	}
	if (message[18] > RK_AUTH_LEVEL_MAX || message[19]) {
		error = -EINVAL;
		goto out;
	}
	epoch = rk_read_be64(message + 2);
	sequence = rk_read_be64(message + 10);
	if (epoch != receiver->epoch) {
		error = -ESTALE;
		goto out;
	}
	if (receiver->exhausted) {
		error = -EOVERFLOW;
		goto out;
	}
	if (sequence < receiver->next_sequence) {
		error = -EALREADY;
		goto out;
	}
	if (sequence > receiver->next_sequence) {
		error = -ERANGE;
		goto out;
	}

	receiver->output_level = message[18];
	receiver->accepted++;
	if (sequence == ~(u64)0)
		receiver->exhausted = true;
	else
		receiver->next_sequence = sequence + 1;
	error = 0;
out:
	rk_platform_unlock(receiver->context);
	rk_platform_clear(digest, sizeof(digest));
	return error;
}
