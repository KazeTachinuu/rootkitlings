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

static void encode_u64(u8 output[8], u64 value)
{
	size_t index = 8;

	while (index) {
		index--;
		output[index] = (u8)value;
		value >>= 8;
	}
}

int rk_dns_signal_build(const u8 key[RK_AUTH_KEY_LENGTH], size_t key_length,
			u64 epoch, u64 sequence, u8 status, u16 transaction_id,
			u8 *packet, size_t capacity, size_t *used)
{
	u8 staging[RK_DNS_SIGNAL_PACKET_LENGTH] = {0};
	u8 message[RK_AUTH_MESSAGE_LENGTH] = {0};
	u8 mac[RK_AUTH_DIGEST_LENGTH] = {0};
	size_t staging_length = 0;
	int result = -EINVAL;

	if (!key || !packet || !used || key_length != RK_AUTH_KEY_LENGTH ||
	    epoch == 0 || sequence == 0 || status > RK_AUTH_LEVEL_MAX)
		goto done;
	if (capacity < RK_DNS_SIGNAL_PACKET_LENGTH) {
		result = -ENOSPC;
		goto done;
	}
	message[0] = RK_AUTH_VERSION;
	message[1] = RK_AUTH_COMMAND_SET_LEVEL;
	encode_u64(message + 2, epoch);
	encode_u64(message + 10, sequence);
	message[18] = status;
	result = rk_platform_sign_once(key, key_length, message,
				       RK_AUTH_BODY_LENGTH, mac);
	if (result)
		goto done;
	memcpy(message + RK_AUTH_BODY_LENGTH, mac, RK_AUTH_TAG_LENGTH);
	result = rk_dns_query_encode(message, transaction_id, staging,
				     sizeof(staging), &staging_length);
	if (!result && staging_length != sizeof(staging))
		result = -EPROTO;
	if (!result) {
		memcpy(packet, staging, sizeof(staging));
		*used = staging_length;
	}
done:
	rk_platform_clear(staging, sizeof(staging));
	rk_platform_clear(message, sizeof(message));
	rk_platform_clear(mac, sizeof(mac));
	return result;
}

int rk_dns_signal_receive(struct rk_receiver *receiver, const u8 *packet,
			  size_t length)
{
	u8 decoded[RK_AUTH_MESSAGE_LENGTH] = {0};
	u16 ignored_id = 0;
	int result;

	if (!receiver || !packet)
		return -EINVAL;
	result = rk_dns_query_decode(packet, length, decoded, &ignored_id);
	if (result == 0)
		result = rk_receiver_submit(receiver, decoded, sizeof(decoded));
	rk_platform_clear(decoded, sizeof(decoded));
	return result;
}
