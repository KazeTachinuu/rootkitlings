// SPDX-License-Identifier: GPL-2.0
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "authenticated_dns.h"
#include "platform.h"

static void rk_write_be64(u8 *output, u64 value)
{
	int index;

	for (index = 7; index >= 0; index--) {
		output[index] = value;
		value >>= 8;
	}
}

int rk_dns_signal_build(const u8 key[RK_AUTH_KEY_LENGTH], size_t key_length,
			u64 epoch, u64 sequence, u8 status, u16 transaction_id,
			u8 *packet, size_t capacity, size_t *used)
{
	u8 candidate[RK_DNS_SIGNAL_PACKET_LENGTH];
	u8 message[RK_AUTH_MESSAGE_LENGTH] = {0};
	u8 digest[RK_AUTH_DIGEST_LENGTH] = {0};
	size_t candidate_length = 0;
	int error;

	if (!key || !packet || !used || key_length != RK_AUTH_KEY_LENGTH ||
	    !epoch || !sequence || status > RK_AUTH_LEVEL_MAX)
		return -EINVAL;
	if (capacity < sizeof(candidate))
		return -ENOSPC;
	message[0] = RK_AUTH_VERSION;
	message[1] = RK_AUTH_COMMAND_SET_LEVEL;
	rk_write_be64(message + 2, epoch);
	rk_write_be64(message + 10, sequence);
	message[18] = status;
	message[19] = 0;
	error = rk_platform_sign_once(key, key_length, message,
				      RK_AUTH_BODY_LENGTH, digest);
	if (error)
		goto out;
	memcpy(message + RK_AUTH_BODY_LENGTH, digest, RK_AUTH_TAG_LENGTH);
	error = rk_dns_query_encode(message, transaction_id, candidate,
				    sizeof(candidate), &candidate_length);
	if (error)
		goto out;
	if (candidate_length != sizeof(candidate)) {
		error = -EPROTO;
		goto out;
	}
	memcpy(packet, candidate, sizeof(candidate));
	*used = sizeof(candidate);
out:
	rk_platform_clear(candidate, sizeof(candidate));
	rk_platform_clear(message, sizeof(message));
	rk_platform_clear(digest, sizeof(digest));
	return error;
}

int rk_dns_signal_receive(struct rk_receiver *receiver, const u8 *packet,
			  size_t length)
{
	u8 message[RK_AUTH_MESSAGE_LENGTH] = {0};
	u16 transaction_id;
	int error;

	if (!receiver || !packet)
		return -EINVAL;
	error = rk_dns_query_decode(packet, length, message, &transaction_id);
	if (!error)
		error = rk_receiver_submit(receiver, message, sizeof(message));
	rk_platform_clear(message, sizeof(message));
	return error;
}
