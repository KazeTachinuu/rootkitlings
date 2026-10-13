/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_SECURE_CONTROL_H
#define RKLINGS_SECURE_CONTROL_H

#include <linux/types.h>

#include "aead.h"
#include "framing.h"

enum {
	RK_SECURE_VERSION = 1,
	RK_SECURE_REQUEST = 1,
	RK_SECURE_REPLY = 2,
	RK_SECURE_HEADER_LENGTH = 18,
	RK_SECURE_PLAINTEXT_MAX =
		RK_FRAME_MAX - RK_SECURE_HEADER_LENGTH - RK_AEAD_TAG_LENGTH,
	RK_SECURE_ENVELOPE_MAX = RK_FRAME_MAX,
	RK_SECURE_SEQUENCE_MAX = 0x7fffffff,
};

struct rk_secure_channel {
	u8 key[RK_AEAD_KEY_LENGTH];
	u64 session;
	u64 next_request;
	u64 next_reply;
	bool request_exhausted;
	bool reply_exhausted;
	bool ready;
};

int rk_secure_channel_start(struct rk_secure_channel *channel,
			    const u8 *key, size_t key_length, u64 session);
void rk_secure_channel_stop(struct rk_secure_channel *channel);
int rk_secure_open_request(struct rk_secure_channel *channel,
			   const u8 *envelope, size_t envelope_length,
			   u8 *plaintext, size_t plaintext_capacity,
			   size_t *plaintext_length);
int rk_secure_seal_reply(struct rk_secure_channel *channel,
			 const u8 *plaintext, size_t plaintext_length,
			 u8 *envelope, size_t envelope_capacity,
			 size_t *envelope_length);

#endif
