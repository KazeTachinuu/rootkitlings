// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/string.h>

#include "authenticated_dns.h"
#include "platform.h"
#include "selftest.h"

static int rk_expect(bool condition, unsigned int *assertions)
{
	(*assertions)++;
	return condition ? 0 : -EINVAL;
}

#define RK_CHECK(condition) do { \
	if (rk_expect((condition), assertions)) { \
		error = -EINVAL; \
		goto out; \
	} \
} while (0)

int rk_dns_signal_selftest(unsigned int *assertions)
{
	static const u8 expected_tag[RK_AUTH_TAG_LENGTH] = {
		0xc7, 0x3d, 0x5c, 0xc1, 0x90, 0xde, 0xe2, 0x9b,
		0x65, 0xdb, 0x60, 0x08, 0xaf, 0x47, 0x1d, 0x12,
	};
	struct rk_receiver receiver = {0};
	struct rk_receiver before;
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];
	u8 packet[RK_DNS_SIGNAL_PACKET_LENGTH];
	u8 changed[RK_DNS_SIGNAL_PACKET_LENGTH];
	u8 decoded[RK_AUTH_MESSAGE_LENGTH];
	size_t used = 0;
	size_t index;
	u16 id = 0;
	u64 epoch = 0x0102030405060708ULL;
	int error = -EINVAL;

	if (!assertions)
		return -EINVAL;
	*assertions = 0;
	for (index = 0; index < sizeof(key); index++) {
		key[index] = index;
		wrong[index] = index ^ 0xa5;
	}
	RK_CHECK(rk_dns_signal_build(key, sizeof(key), epoch, 1, 3, 0x1234,
				     packet, sizeof(packet), &used) == 0);
	RK_CHECK(used == sizeof(packet));
	RK_CHECK(rk_dns_query_decode(packet, used, decoded, &id) == 0);
	RK_CHECK(id == 0x1234 && decoded[0] == RK_AUTH_VERSION &&
		 decoded[1] == RK_AUTH_COMMAND_SET_LEVEL && decoded[18] == 3 &&
		 decoded[19] == 0);
	RK_CHECK(!memcmp(decoded + RK_AUTH_BODY_LENGTH, expected_tag,
			 RK_AUTH_TAG_LENGTH));
	RK_CHECK(rk_receiver_start(&receiver, key, sizeof(key), epoch, 1) == 0);
	RK_CHECK(rk_dns_signal_receive(&receiver, packet, used) == 0);
	RK_CHECK(receiver.accepted == 1 && receiver.next_sequence == 2 &&
		 receiver.output_level == 3);

	before = receiver;
	RK_CHECK(rk_dns_signal_receive(&receiver, packet, used) == -EALREADY);
	RK_CHECK(!memcmp(&before, &receiver, sizeof(before)));
	memcpy(changed, packet, sizeof(changed));
	changed[17] = changed[17] == '0' ? '1' : '0';
	RK_CHECK(rk_dns_signal_receive(&receiver, changed, sizeof(changed)) ==
		 -EBADMSG);
	RK_CHECK(!memcmp(&before, &receiver, sizeof(before)));
	RK_CHECK(rk_dns_signal_build(wrong, sizeof(wrong), epoch, 2, 1, 0x2222,
				     changed, sizeof(changed), &used) == 0);
	RK_CHECK(rk_dns_signal_receive(&receiver, changed, used) == -EBADMSG);
	RK_CHECK(!memcmp(&before, &receiver, sizeof(before)));
	RK_CHECK(rk_dns_signal_build(key, sizeof(key), epoch + 1, 2, 1, 0x2222,
				     changed, sizeof(changed), &used) == 0);
	RK_CHECK(rk_dns_signal_receive(&receiver, changed, used) == -ESTALE);
	RK_CHECK(!memcmp(&before, &receiver, sizeof(before)));
	RK_CHECK(rk_dns_signal_build(key, sizeof(key), epoch, 3, 1, 0x2222,
				     changed, sizeof(changed), &used) == 0);
	RK_CHECK(rk_dns_signal_receive(&receiver, changed, used) == -ERANGE);
	RK_CHECK(!memcmp(&before, &receiver, sizeof(before)));
	for (index = 0; index < sizeof(packet); index++) {
		RK_CHECK(rk_dns_signal_receive(&receiver, packet, index) ==
			 -EMSGSIZE);
		RK_CHECK(!memcmp(&before, &receiver, sizeof(before)));
	}
	RK_CHECK(rk_dns_signal_build(key, sizeof(key), epoch, 2, 1, 0x2222,
				     changed, sizeof(changed), &used) == 0);
	RK_CHECK(rk_dns_signal_receive(&receiver, changed, used) == 0);
	RK_CHECK(receiver.accepted == 2 && receiver.next_sequence == 3 &&
		 receiver.output_level == 1);
	error = 0;
out:
	rk_receiver_stop(&receiver);
	rk_platform_clear(key, sizeof(key));
	rk_platform_clear(wrong, sizeof(wrong));
	rk_platform_clear(packet, sizeof(packet));
	rk_platform_clear(changed, sizeof(changed));
	rk_platform_clear(decoded, sizeof(decoded));
	return error;
}
