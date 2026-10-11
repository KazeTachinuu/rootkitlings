// SPDX-License-Identifier: GPL-2.0
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "secure_control.h"

// TODO: Implement the secure channel functions.
//
// rk_secure_clear: zeroize a buffer (use memset under __KERNEL__, memset otherwise)
//
// rk_nonce: build a 12-byte nonce from (session, sequence, kind).
//   Bytes 0-7: session as big-endian u64
//   Bytes 8-11: (sequence << 1 | (kind == REPLY)) as big-endian u32
//   Validate: session != 0, sequence in [1, SEQUENCE_MAX],
//   kind is REQUEST or REPLY
//
// rk_secure_channel_start: copy key, set session, init counters to 1
//   Reject if channel already ready or key wrong length
//
// rk_secure_channel_stop: zeroize the entire channel struct
//
// rk_secure_open_request: decrypt an incoming request envelope.
//   Parse header (version, kind, session, sequence), build nonce,
//   call rk_aead_open(), verify version==1, kind==REQUEST,
//   session matches, sequence == next_request (replay defense).
//   Advance next_request on success.
//
// rk_secure_seal_reply: encrypt an outgoing reply.
//   Build header (version, REPLY, session, sequence), build nonce,
//   call rk_aead_seal() with header as AAD and plaintext as message.
//   Advance next_reply on success.

void rk_secure_clear(void *buffer, size_t length)
{
	if (buffer && length)
		memset(buffer, 0, length);
}

int rk_secure_channel_start(struct rk_secure_channel *channel,
			    const u8 *key, size_t key_length, u64 session)
{
	(void)channel;
	(void)key;
	(void)key_length;
	(void)session;
	return -ENOSYS;
}

void rk_secure_channel_stop(struct rk_secure_channel *channel)
{
	if (channel)
		rk_secure_clear(channel, sizeof(*channel));
}

int rk_secure_open_request(struct rk_secure_channel *channel,
			   const u8 *envelope, size_t envelope_length,
			   u8 *plaintext, size_t plaintext_capacity,
			   size_t *plaintext_length)
{
	(void)channel;
	(void)envelope;
	(void)envelope_length;
	(void)plaintext;
	(void)plaintext_capacity;
	(void)plaintext_length;
	return -ENOSYS;
}

int rk_secure_seal_reply(struct rk_secure_channel *channel,
			 const u8 *plaintext, size_t plaintext_length,
			 u8 *envelope, size_t envelope_capacity,
			 size_t *envelope_length)
{
	(void)channel;
	(void)plaintext;
	(void)plaintext_length;
	(void)envelope;
	(void)envelope_capacity;
	(void)envelope_length;
	return -ENOSYS;
}
