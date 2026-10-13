// SPDX-License-Identifier: GPL-2.0
#include <linux/unaligned.h>
#include <linux/string.h>

#include "secure_control.h"

static int rk_nonce(u64 session, u64 sequence, u8 kind,
		    u8 nonce[RK_AEAD_NONCE_LENGTH])
{
	u32 counter;

	if (!session || !sequence || sequence > RK_SECURE_SEQUENCE_MAX ||
	    (kind != RK_SECURE_REQUEST && kind != RK_SECURE_REPLY))
		return -EINVAL;
	put_unaligned_be64(session, nonce);
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
	struct rk_secure_channel candidate = { 0 };

	if (!channel || !key || key_length != RK_AEAD_KEY_LENGTH || !session)
		return -EINVAL;
	if (channel->ready)
		return -EBUSY;
	memcpy(candidate.key, key, sizeof(candidate.key));
	candidate.session = session;
	candidate.next_request = 1;
	candidate.next_reply = 1;
	candidate.ready = true;
	*channel = candidate;
	memzero_explicit(&candidate, sizeof(candidate));
	return 0;
}

void rk_secure_channel_stop(struct rk_secure_channel *channel)
{
	if (channel)
		memzero_explicit(channel, sizeof(*channel));
}

int rk_secure_open_request(struct rk_secure_channel *channel,
			   const u8 *envelope, size_t envelope_length,
			   u8 *plaintext, size_t plaintext_capacity,
			   size_t *plaintext_length)
{
	u8 nonce[RK_AEAD_NONCE_LENGTH] = { 0 };
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
	session = get_unaligned_be64(envelope + 2);
	sequence = get_unaligned_be64(envelope + 10);
	error = rk_nonce(session, sequence, envelope[1], nonce);
	if (error)
		goto out;
	*plaintext_length = 0;
	error = rk_aead_open(channel->key, nonce, envelope,
			     RK_SECURE_HEADER_LENGTH,
			     envelope + RK_SECURE_HEADER_LENGTH,
			     envelope_length - RK_SECURE_HEADER_LENGTH,
			     plaintext, plaintext_capacity, plaintext_length);
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
	if (sequence == RK_SECURE_SEQUENCE_MAX)
		channel->request_exhausted = true;
	else
		channel->next_request = sequence + 1;
	error = 0;
out:
	if (error) {
		memzero_explicit(plaintext, plaintext_capacity);
		*plaintext_length = 0;
	}
	memzero_explicit(nonce, sizeof(nonce));
	return error;
}

int rk_secure_seal_reply(struct rk_secure_channel *channel,
			 const u8 *plaintext, size_t plaintext_length,
			 u8 *envelope, size_t envelope_capacity,
			 size_t *envelope_length)
{
	u8 nonce[RK_AEAD_NONCE_LENGTH] = { 0 };
	size_t sealed_length = 0;
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
	envelope[0] = RK_SECURE_VERSION;
	envelope[1] = RK_SECURE_REPLY;
	put_unaligned_be64(channel->session, envelope + 2);
	put_unaligned_be64(sequence, envelope + 10);
	error = rk_nonce(channel->session, sequence, RK_SECURE_REPLY, nonce);
	if (error)
		goto out;
	error = rk_aead_seal(channel->key, nonce, envelope,
			     RK_SECURE_HEADER_LENGTH, plaintext, plaintext_length,
			     envelope + RK_SECURE_HEADER_LENGTH,
			     envelope_capacity - RK_SECURE_HEADER_LENGTH,
			     &sealed_length);
	if (error)
		goto out;
	if (sealed_length != plaintext_length + RK_AEAD_TAG_LENGTH) {
		error = -EPROTO;
		goto out;
	}
	*envelope_length = RK_SECURE_HEADER_LENGTH + sealed_length;
	if (sequence == RK_SECURE_SEQUENCE_MAX)
		channel->reply_exhausted = true;
	else
		channel->next_reply = sequence + 1;
	error = 0;
out:
	if (error) {
		memzero_explicit(envelope, envelope_capacity);
		*envelope_length = 0;
	}
	memzero_explicit(nonce, sizeof(nonce));
	return error;
}
