// SPDX-License-Identifier: GPL-2.0
#include "authenticated.h"

int rk_receiver_start(struct rk_receiver *receiver,
		      const u8 key[RK_AUTH_KEY_LENGTH], size_t key_length,
		      u64 epoch, u64 first_sequence)
{
	(void)receiver;
	(void)key;
	(void)key_length;
	(void)epoch;
	(void)first_sequence;
	return -EINVAL;
}

void rk_receiver_stop(struct rk_receiver *receiver)
{
	(void)receiver;
}

int rk_receiver_submit(struct rk_receiver *receiver, const u8 *message,
		       size_t message_length)
{
	(void)receiver;
	(void)message;
	(void)message_length;
	return -EINVAL;
}
