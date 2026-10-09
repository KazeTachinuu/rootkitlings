// SPDX-License-Identifier: GPL-2.0
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

#include "authenticated_dns.h"

int rk_dns_signal_build(const u8 key[RK_AUTH_KEY_LENGTH], size_t key_length,
			u64 epoch, u64 sequence, u8 status, u16 transaction_id,
			u8 *packet, size_t capacity, size_t *used)
{
	/* Build and authenticate one complete DNS-shaped signal. */
	(void)key;
	(void)key_length;
	(void)epoch;
	(void)sequence;
	(void)status;
	(void)transaction_id;
	(void)packet;
	(void)capacity;
	(void)used;
	return -EINVAL;
}

int rk_dns_signal_receive(struct rk_receiver *receiver, const u8 *packet,
			  size_t length)
{
	/* Decode, authenticate, and commit one fresh signal. */
	(void)receiver;
	(void)packet;
	(void)length;
	return -EINVAL;
}
