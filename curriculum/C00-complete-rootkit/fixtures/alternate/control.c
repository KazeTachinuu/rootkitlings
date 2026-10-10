// SPDX-License-Identifier: GPL-2.0
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "auth_platform.h"
#include "secure_control.h"

static void store64(u8 *bytes, u64 value)
{
	int index;

	for (index = 7; index >= 0; index--) {
		bytes[index] = (u8)value;
		value >>= 8;
	}
}

static u64 load64(const u8 *bytes)
{
	u64 value = 0;
	size_t index;

	for (index = 0; index < 8; index++)
		value = (value << 8) | bytes[index];
	return value;
}

static int make_nonce(u64 session, u64 sequence, u8 kind,
		    u8 nonce[RK_AEAD_NONCE_LENGTH])
{
	u32 counter;

	if (!session || !sequence || sequence > RK_SECURE_SEQUENCE_MAX ||
	    (kind != RK_SECURE_REQUEST && kind != RK_SECURE_REPLY))
		return -EINVAL;
	store64(nonce, session);
	counter = (u32)(sequence << 1) | (kind == RK_SECURE_REPLY);
	nonce[8] = (u8)(counter >> 24);
	nonce[9] = (u8)(counter >> 16);
	nonce[10] = (u8)(counter >> 8);
	nonce[11] = (u8)counter;
	return 0;
}

int rk_secure_channel_start(struct rk_secure_channel *channel,
			    const u8 *key, size_t key_length, u64 session)
{
	struct rk_secure_channel staging = { 0 };

	if (!channel || !key || key_length != RK_AEAD_KEY_LENGTH || !session)
		return -EINVAL;
	if (channel->ready)
		return -EBUSY;
	memcpy(staging.key, key, sizeof(staging.key));
	staging.session = session;
	staging.next_request = 1;
	staging.next_reply = 1;
	staging.ready = true;
	*channel = staging;
	rk_platform_clear(&staging, sizeof(staging));
	return 0;
}

void rk_secure_channel_stop(struct rk_secure_channel *channel)
{
	if (channel)
		rk_platform_clear(channel, sizeof(*channel));
}

int rk_secure_open_request(struct rk_secure_channel *channel,
			   const u8 *envelope, size_t envelope_length,
			   u8 *plaintext, size_t plaintext_capacity,
			   size_t *plaintext_length)
{
	u8 staging[RK_SECURE_PLAINTEXT_MAX] = { 0 };
	u8 nonce[RK_AEAD_NONCE_LENGTH] = { 0 };
	size_t staging_length = 0;
	u64 session;
	u64 sequence;
	int error;

	if (!channel || !envelope || !plaintext || !plaintext_length)
		return -EINVAL;
	if (!channel->ready)
		return -ENODEV;
	if (envelope_length < RK_SECURE_HEADER_LENGTH + RK_AEAD_TAG_LENGTH ||
	    envelope_length > RK_SECURE_ENVELOPE_MAX)
		return -EMSGSIZE;
	if (plaintext_capacity < envelope_length - RK_SECURE_HEADER_LENGTH -
				 RK_AEAD_TAG_LENGTH)
		return -ENOSPC;
	session = load64(envelope + 2);
	sequence = load64(envelope + 10);
	error = make_nonce(session, sequence, envelope[1], nonce);
	if (error)
		goto out;
	error = rk_aead_open(channel->key, nonce, envelope,
			     RK_SECURE_HEADER_LENGTH,
			     envelope + RK_SECURE_HEADER_LENGTH,
			     envelope_length - RK_SECURE_HEADER_LENGTH,
			     staging, sizeof(staging), &staging_length);
	if (error)
		goto out;
	if (envelope[0] != RK_SECURE_VERSION) {
		error = -EPROTONOSUPPORT;
		goto out;
	}
	if (envelope[1] != RK_SECURE_REQUEST) {
		error = -EOPNOTSUPP;
		goto out;
	}
	if (session != channel->session) {
		error = -ESTALE;
		goto out;
	}
	if (channel->request_exhausted) {
		error = -EOVERFLOW;
		goto out;
	}
	if (sequence < channel->next_request) {
		error = -EALREADY;
		goto out;
	}
	if (sequence > channel->next_request) {
		error = -ERANGE;
		goto out;
	}
	memcpy(plaintext, staging, staging_length);
	*plaintext_length = staging_length;
	if (sequence == RK_SECURE_SEQUENCE_MAX)
		channel->request_exhausted = true;
	else
		channel->next_request = sequence + 1;
	error = 0;
out:
	rk_platform_clear(staging, sizeof(staging));
	rk_platform_clear(nonce, sizeof(nonce));
	return error;
}

int rk_secure_seal_reply(struct rk_secure_channel *channel,
			 const u8 *plaintext, size_t plaintext_length,
			 u8 *envelope, size_t envelope_capacity,
			 size_t *envelope_length)
{
	u8 staging[RK_SECURE_ENVELOPE_MAX] = { 0 };
	u8 nonce[RK_AEAD_NONCE_LENGTH] = { 0 };
	size_t cipher_length = 0;
	u64 sequence;
	int error;

	if (!channel || !plaintext || !envelope || !envelope_length)
		return -EINVAL;
	if (!channel->ready)
		return -ENODEV;
	if (plaintext_length > RK_SECURE_PLAINTEXT_MAX)
		return -EMSGSIZE;
	if (envelope_capacity < RK_SECURE_HEADER_LENGTH + plaintext_length +
				RK_AEAD_TAG_LENGTH)
		return -ENOSPC;
	if (channel->reply_exhausted)
		return -EOVERFLOW;
	sequence = channel->next_reply;
	staging[0] = RK_SECURE_VERSION;
	staging[1] = RK_SECURE_REPLY;
	store64(staging + 2, channel->session);
	store64(staging + 10, sequence);
	error = make_nonce(channel->session, sequence, RK_SECURE_REPLY, nonce);
	if (error)
		goto out;
	error = rk_aead_seal(channel->key, nonce, staging,
			     RK_SECURE_HEADER_LENGTH, plaintext, plaintext_length,
			     staging + RK_SECURE_HEADER_LENGTH,
			     sizeof(staging) - RK_SECURE_HEADER_LENGTH,
			     &cipher_length);
	if (error)
		goto out;
	if (cipher_length != plaintext_length + RK_AEAD_TAG_LENGTH) {
		error = -EPROTO;
		goto out;
	}
	memcpy(envelope, staging, RK_SECURE_HEADER_LENGTH + cipher_length);
	*envelope_length = RK_SECURE_HEADER_LENGTH + cipher_length;
	if (sequence == RK_SECURE_SEQUENCE_MAX)
		channel->reply_exhausted = true;
	else
		channel->next_reply = sequence + 1;
	error = 0;
out:
	rk_platform_clear(staging, sizeof(staging));
	rk_platform_clear(nonce, sizeof(nonce));
	return error;
}
