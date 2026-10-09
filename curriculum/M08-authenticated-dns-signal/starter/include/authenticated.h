/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_AUTHENTICATED_H
#define ROOTKITLINGS_AUTHENTICATED_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint64_t u64;
#endif

enum {
	RK_AUTH_KEY_LENGTH = 32,
	RK_AUTH_DIGEST_LENGTH = 32,
	RK_AUTH_TAG_LENGTH = 16,
	RK_AUTH_BODY_LENGTH = 20,
	RK_AUTH_MESSAGE_LENGTH = RK_AUTH_BODY_LENGTH + RK_AUTH_TAG_LENGTH,
	RK_AUTH_VERSION = 1,
	RK_AUTH_COMMAND_SET_LEVEL = 1,
	RK_AUTH_LEVEL_MAX = 3,
};

struct rk_receiver {
	void *context;
	u64 epoch;
	u64 next_sequence;
	u64 accepted;
	unsigned int output_level;
	bool exhausted;
	bool ready;
};

int rk_receiver_start(struct rk_receiver *receiver,
		      const u8 key[RK_AUTH_KEY_LENGTH], size_t key_length,
		      u64 epoch, u64 first_sequence);
void rk_receiver_stop(struct rk_receiver *receiver);
int rk_receiver_submit(struct rk_receiver *receiver, const u8 *message,
		       size_t message_length);

#endif
