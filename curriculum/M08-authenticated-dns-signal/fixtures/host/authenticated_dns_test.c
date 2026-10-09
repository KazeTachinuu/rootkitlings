#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "authenticated_dns.h"
#include "auth_platform.h"

static unsigned long assertions;

void rk_test_fail_at(int point);
unsigned int rk_test_live_contexts(void);
void rk_test_clear_probe_reset(void);
unsigned int rk_test_clear_calls(size_t length);
unsigned int rk_test_clear_nonzero_before(size_t length);
unsigned int rk_test_clear_zero_after(size_t length);

#define CHECK(condition) do { \
	assertions++; \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void fill_keys(u8 key[RK_AUTH_KEY_LENGTH],
		      u8 wrong[RK_AUTH_KEY_LENGTH])
{
	size_t index;

	for (index = 0; index < RK_AUTH_KEY_LENGTH; index++) {
		key[index] = (u8)index;
		wrong[index] = (u8)(index ^ 0xa5);
	}
}

static void expect_build_failure(const u8 *key, size_t key_length,
				 u64 epoch, u64 sequence, u8 status,
				 size_t capacity, int expected)
{
	u8 packet[RK_DNS_SIGNAL_PACKET_LENGTH + 8];
	u8 before[sizeof(packet)];
	size_t used = 0xa5a5;

	memset(packet, 0x5a, sizeof(packet));
	memcpy(before, packet, sizeof(packet));
	CHECK(rk_dns_signal_build(key, key_length, epoch, sequence, status,
				  0x1234, packet, capacity, &used) == expected);
	CHECK(used == 0xa5a5);
	CHECK(!memcmp(packet, before, sizeof(packet)));
}

static void expect_receive_unchanged(struct rk_receiver *receiver,
				     const u8 *packet, size_t length)
{
	struct rk_receiver before = *receiver;

	CHECK(rk_dns_signal_receive(receiver, packet, length) != 0);
	CHECK(!memcmp(&before, receiver, sizeof(before)));
}

static void test_build_contract(void)
{
	static const u8 expected_tag[RK_AUTH_TAG_LENGTH] = {
		0xc7, 0x3d, 0x5c, 0xc1, 0x90, 0xde, 0xe2, 0x9b,
		0x65, 0xdb, 0x60, 0x08, 0xaf, 0x47, 0x1d, 0x12,
	};
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];
	u8 packet[RK_DNS_SIGNAL_PACKET_LENGTH + 8];
	u8 message[RK_AUTH_MESSAGE_LENGTH];
	size_t used = 0;
	size_t capacity;
	u16 id = 0;

	fill_keys(key, wrong);
	memset(packet, 0xa5, sizeof(packet));
	rk_test_clear_probe_reset();
	CHECK(rk_dns_signal_build(key, sizeof(key), 0x0102030405060708ULL,
				  1, 3, 0x1234, packet,
				  RK_DNS_SIGNAL_PACKET_LENGTH, &used) == 0);
	CHECK(rk_test_clear_calls(RK_DNS_SIGNAL_PACKET_LENGTH) == 1);
	CHECK(rk_test_clear_calls(RK_AUTH_MESSAGE_LENGTH) == 1);
	CHECK(rk_test_clear_calls(RK_AUTH_DIGEST_LENGTH) == 1);
	CHECK(rk_test_clear_nonzero_before(RK_DNS_SIGNAL_PACKET_LENGTH) == 1);
	CHECK(rk_test_clear_nonzero_before(RK_AUTH_MESSAGE_LENGTH) == 1);
	CHECK(rk_test_clear_nonzero_before(RK_AUTH_DIGEST_LENGTH) == 1);
	CHECK(rk_test_clear_zero_after(RK_DNS_SIGNAL_PACKET_LENGTH) == 1);
	CHECK(rk_test_clear_zero_after(RK_AUTH_MESSAGE_LENGTH) == 1);
	CHECK(rk_test_clear_zero_after(RK_AUTH_DIGEST_LENGTH) == 1);
	CHECK(used == RK_DNS_SIGNAL_PACKET_LENGTH);
	CHECK(packet[used] == 0xa5);
	CHECK(rk_dns_query_decode(packet, used, message, &id) == 0);
	CHECK(id == 0x1234);
	CHECK(message[0] == 1 && message[1] == 1 && message[18] == 3 &&
	      message[19] == 0);
	CHECK(!memcmp(message + RK_AUTH_BODY_LENGTH, expected_tag,
		      sizeof(expected_tag)));
	CHECK(packet[12] == 3 && !memcmp(packet + 13, "rk1", 3));
	CHECK(packet[16] == 40 && packet[57] == 32 && packet[90] == 12 &&
	      packet[103] == 7 && packet[111] == 0);
	CHECK(!memcmp(packet + 91, "rootkitlings", 12));
	CHECK(!memcmp(packet + 104, "example", 7));
	CHECK(packet[112] == 0 && packet[113] == 16 &&
	      packet[114] == 0 && packet[115] == 1);

	for (capacity = 0; capacity < RK_DNS_SIGNAL_PACKET_LENGTH; capacity++)
		expect_build_failure(key, sizeof(key), 1, 1, 0, capacity,
				     -ENOSPC);
	expect_build_failure(NULL, sizeof(key), 1, 1, 0,
			     RK_DNS_SIGNAL_PACKET_LENGTH, -EINVAL);
	expect_build_failure(key, sizeof(key) - 1, 1, 1, 0,
			     RK_DNS_SIGNAL_PACKET_LENGTH, -EINVAL);
	expect_build_failure(key, sizeof(key) + 1, 1, 1, 0,
			     RK_DNS_SIGNAL_PACKET_LENGTH, -EINVAL);
	expect_build_failure(key, sizeof(key), 0, 1, 0,
			     RK_DNS_SIGNAL_PACKET_LENGTH, -EINVAL);
	expect_build_failure(key, sizeof(key), 1, 0, 0,
			     RK_DNS_SIGNAL_PACKET_LENGTH, -EINVAL);
	expect_build_failure(key, sizeof(key), 1, 1, 4,
			     RK_DNS_SIGNAL_PACKET_LENGTH, -EINVAL);
	used = 0xa5a5;
	CHECK(rk_dns_signal_build(key, sizeof(key), 1, 1, 0, 0x1234,
				  NULL, RK_DNS_SIGNAL_PACKET_LENGTH, &used) ==
	      -EINVAL);
	CHECK(used == 0xa5a5);
	memset(packet, 0x5a, sizeof(packet));
	CHECK(rk_dns_signal_build(key, sizeof(key), 1, 1, 0, 0x1234,
				  packet, RK_DNS_SIGNAL_PACKET_LENGTH, NULL) ==
	      -EINVAL);
	for (capacity = 0; capacity < sizeof(packet); capacity++)
		CHECK(packet[capacity] == 0x5a);

	for (int point = 1; point <= 3; point++) {
		int expected = point == 1 ? -ENOMEM :
			       point == 2 ? -EKEYREJECTED : -EIO;

		rk_test_fail_at(point);
		expect_build_failure(key, sizeof(key), 1, 1, 0,
				     RK_DNS_SIGNAL_PACKET_LENGTH, expected);
		rk_test_fail_at(0);
	}
	CHECK(rk_test_live_contexts() == 0);
}

static void test_receive_and_replay(void)
{
	struct rk_receiver receiver = {0};
	struct rk_receiver before;
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];
	u8 first[RK_DNS_SIGNAL_PACKET_LENGTH];
	u8 next[RK_DNS_SIGNAL_PACKET_LENGTH];
	u8 changed[RK_DNS_SIGNAL_PACKET_LENGTH];
	size_t used;
	size_t index;
	u64 epoch = 0x0102030405060708ULL;

	fill_keys(key, wrong);
	CHECK(rk_dns_signal_build(key, sizeof(key), epoch, 1, 3, 0x1234,
				  first, sizeof(first), &used) == 0);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), epoch, 1) == 0);
	before = receiver;
	CHECK(rk_dns_signal_receive(NULL, first, used) == -EINVAL);
	CHECK(rk_dns_signal_receive(&receiver, NULL, used) == -EINVAL);
	CHECK(!memcmp(&before, &receiver, sizeof(before)));
	rk_test_clear_probe_reset();
	CHECK(rk_dns_signal_receive(&receiver, first, used) == 0);
	CHECK(rk_test_clear_calls(RK_AUTH_MESSAGE_LENGTH) == 1);
	CHECK(rk_test_clear_nonzero_before(RK_AUTH_MESSAGE_LENGTH) == 1);
	CHECK(rk_test_clear_zero_after(RK_AUTH_MESSAGE_LENGTH) == 1);
	CHECK(receiver.accepted == 1 && receiver.next_sequence == 2 &&
	      receiver.output_level == 3);
	expect_receive_unchanged(&receiver, first, used);

	CHECK(rk_dns_signal_build(key, sizeof(key), epoch, 2, 1, 0x2222,
				  next, sizeof(next), &used) == 0);
	before = receiver;
	for (index = 2; index < sizeof(next); index++) {
		memcpy(changed, next, sizeof(changed));
		changed[index] ^= 1;
		expect_receive_unchanged(&receiver, changed, sizeof(changed));
		CHECK(!memcmp(&before, &receiver, sizeof(before)));
	}
	for (index = 0; index < sizeof(next); index++)
		expect_receive_unchanged(&receiver, next, index);
	for (index = sizeof(next) + 1; index <= sizeof(next) + 16; index++)
		expect_receive_unchanged(&receiver, next, index);

	CHECK(rk_dns_signal_build(wrong, sizeof(wrong), epoch, 2, 1, 0x2222,
				  changed, sizeof(changed), &used) == 0);
	expect_receive_unchanged(&receiver, changed, used);
	CHECK(rk_dns_signal_build(key, sizeof(key), epoch + 1, 2, 1, 0x2222,
				  changed, sizeof(changed), &used) == 0);
	expect_receive_unchanged(&receiver, changed, used);
	CHECK(rk_dns_signal_build(key, sizeof(key), epoch, 3, 1, 0x2222,
				  changed, sizeof(changed), &used) == 0);
	expect_receive_unchanged(&receiver, changed, used);

	/* The resolver-facing transaction ID is deliberately outside the
	 * authenticated status body.  Changing it must not alter replay state. */
	next[0] = 0xab;
	next[1] = 0xcd;
	CHECK(rk_dns_signal_receive(&receiver, next, sizeof(next)) == 0);
	CHECK(receiver.accepted == 2 && receiver.next_sequence == 3 &&
	      receiver.output_level == 1);
	rk_receiver_stop(&receiver);
	CHECK(rk_test_live_contexts() == 0);
}

int main(void)
{
	test_build_contract();
	test_receive_and_replay();
	printf("[+] authenticated DNS signal: %lu assertions\n", assertions);
	return EXIT_SUCCESS;
}
