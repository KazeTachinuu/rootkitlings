// SPDX-License-Identifier: GPL-2.0
#include "secure_control.h"

int rk_secure_channel_start(struct rk_secure_channel *channel,
			    const u8 *key, size_t key_length, u64 session)
{
	(void)channel;
	(void)key;
	(void)key_length;
	(void)session;
	return -EOPNOTSUPP;
}

void rk_secure_channel_stop(struct rk_secure_channel *channel)
{
	(void)channel;
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
	return -EOPNOTSUPP;
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
	return -EOPNOTSUPP;
}
