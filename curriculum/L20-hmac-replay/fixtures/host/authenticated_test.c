#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "authenticated.h"
#include "auth_platform.h"

enum {
	THREADS = 32,
	ROUNDS = 64,
};

static unsigned long assertions;

void rk_test_fail_at(int point);
unsigned int rk_test_live_contexts(void);
unsigned int rk_test_erased_contexts(void);
bool rk_test_digest_was_cleared(void);
void rk_test_force_tag_result(bool enabled, bool result);
unsigned int rk_test_tag_comparisons(void);
size_t rk_test_tag_bytes(void);
size_t rk_test_tag_length(void);
bool rk_test_tag_compared_while_locked(void);

#define CHECK(condition)                                               \
	do {                                                           \
		assertions++;                                          \
		if (!(condition)) {                                    \
			fprintf(stderr, "check failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #condition);       \
			exit(EXIT_FAILURE);                            \
		}                                                      \
	} while (0)

static void write_be64(u8 *bytes, u64 value)
{
	int index;

	for (index = 7; index >= 0; index--) {
		bytes[index] = (u8)value;
		value >>= 8;
	}
}

static void sign_message(const u8 key[RK_AUTH_KEY_LENGTH], u64 epoch,
			 u64 sequence, u8 version, u8 command, u8 level,
			 u8 reserved, u8 message[RK_AUTH_MESSAGE_LENGTH])
{
	u8 digest[RK_AUTH_DIGEST_LENGTH] = {0};

	memset(message, 0, RK_AUTH_MESSAGE_LENGTH);
	message[0] = version;
	message[1] = command;
	write_be64(message + 2, epoch);
	write_be64(message + 10, sequence);
	message[18] = level;
	message[19] = reserved;
	CHECK(rk_platform_sign_once(key, RK_AUTH_KEY_LENGTH, message,
				    RK_AUTH_BODY_LENGTH, digest) == 0);
	memcpy(message + RK_AUTH_BODY_LENGTH, digest, RK_AUTH_TAG_LENGTH);
	rk_platform_clear(digest, sizeof(digest));
}

static void expect_unchanged(struct rk_receiver *receiver, const u8 *message,
			     size_t length, int expected)
{
	struct rk_receiver before = *receiver;

	CHECK(rk_receiver_submit(receiver, message, length) == expected);
	CHECK(rk_test_digest_was_cleared());
	CHECK(!memcmp(&before, receiver, sizeof(before)));
}

static void fill_keys(u8 key[RK_AUTH_KEY_LENGTH], u8 wrong[RK_AUTH_KEY_LENGTH])
{
	size_t index;

	for (index = 0; index < RK_AUTH_KEY_LENGTH; index++) {
		key[index] = (u8)index;
		wrong[index] = (u8)(index ^ 0xa5);
	}
}

static void test_rfc4231_oracle(void)
{
	/* RFC 4231, section 4.2, test case 1. */
	static const u8 expected[RK_AUTH_DIGEST_LENGTH] = {
	    0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf,
	    0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83,
	    0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7,
	};
	static const u8 data[] = "Hi There";
	u8 key[20];
	u8 digest[RK_AUTH_DIGEST_LENGTH];

	memset(key, 0x0b, sizeof(key));
	CHECK(rk_platform_sign_once(key, sizeof(key), data, sizeof(data) - 1,
				    digest) == 0);
	CHECK(!memcmp(digest, expected, sizeof(expected)));
	CHECK(rk_test_live_contexts() == 0);
	rk_platform_clear(key, sizeof(key));
	rk_platform_clear(digest, sizeof(digest));
}

static void test_known_answer_and_key_ownership(void)
{
	static const u8 expected[RK_AUTH_DIGEST_LENGTH] = {
	    0xc7, 0x3d, 0x5c, 0xc1, 0x90, 0xde, 0xe2, 0x9b, 0x65, 0xdb, 0x60,
	    0x08, 0xaf, 0x47, 0x1d, 0x12, 0xd7, 0x09, 0x89, 0x35, 0x8f, 0x99,
	    0xc9, 0xe6, 0xfb, 0x49, 0xa4, 0x66, 0x14, 0x6d, 0x16, 0xf1,
	};
	struct rk_receiver receiver = {0};
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 signing_key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];
	u8 message[RK_AUTH_MESSAGE_LENGTH];
	u8 digest[RK_AUTH_DIGEST_LENGTH];

	fill_keys(key, wrong);
	memcpy(signing_key, key, sizeof(key));
	sign_message(signing_key, 0x0102030405060708ULL, 1, RK_AUTH_VERSION,
		     RK_AUTH_COMMAND_SET_LEVEL, 3, 0, message);
	CHECK(rk_platform_sign_once(signing_key, sizeof(signing_key), message,
				    RK_AUTH_BODY_LENGTH, digest) == 0);
	CHECK(!memcmp(digest, expected, sizeof(expected)));
	CHECK(!memcmp(message + RK_AUTH_BODY_LENGTH, expected,
		      RK_AUTH_TAG_LENGTH));
	CHECK(message[0] == 1 && message[1] == 1 && message[18] == 3);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key),
				0x0102030405060708ULL, 1) == 0);
	memset(key, 0xa5, sizeof(key));
	CHECK(rk_receiver_submit(&receiver, message, sizeof(message)) == 0);
	CHECK(rk_test_digest_was_cleared());
	CHECK(receiver.output_level == 3 && receiver.accepted == 1);
	rk_receiver_stop(&receiver);
	CHECK(rk_test_live_contexts() == 0);
	memset(signing_key, 0, sizeof(signing_key));
	memset(wrong, 0, sizeof(wrong));
	memset(message, 0, sizeof(message));
	memset(digest, 0, sizeof(digest));
}

static void test_fail_closed_matrix(void)
{
	struct rk_receiver receiver = {0};
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];
	u8 valid[RK_AUTH_MESSAGE_LENGTH];
	u8 changed[RK_AUTH_MESSAGE_LENGTH];
	u64 epoch = 0x1122334455667788ULL;
	size_t byte;
	unsigned int bit;

	fill_keys(key, wrong);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), epoch, 1) == 0);
	sign_message(key, epoch, 1, RK_AUTH_VERSION, RK_AUTH_COMMAND_SET_LEVEL,
		     2, 0, valid);
	for (byte = 0; byte < sizeof(valid); byte++) {
		for (bit = 0; bit < 8; bit++) {
			memcpy(changed, valid, sizeof(changed));
			changed[byte] ^= (u8)(1U << bit);
			expect_unchanged(&receiver, changed, sizeof(changed),
					 -EBADMSG);
		}
	}
	for (byte = 0; byte <= 128; byte++) {
		if (byte != RK_AUTH_MESSAGE_LENGTH)
			expect_unchanged(&receiver, valid, byte, -EMSGSIZE);
	}

	sign_message(wrong, epoch, 1, RK_AUTH_VERSION,
		     RK_AUTH_COMMAND_SET_LEVEL, 2, 0, changed);
	expect_unchanged(&receiver, changed, sizeof(changed), -EBADMSG);
	sign_message(key, epoch + 1, 1, RK_AUTH_VERSION,
		     RK_AUTH_COMMAND_SET_LEVEL, 2, 0, changed);
	expect_unchanged(&receiver, changed, sizeof(changed), -ESTALE);
	sign_message(key, epoch, 1, 2, RK_AUTH_COMMAND_SET_LEVEL, 2, 0,
		     changed);
	expect_unchanged(&receiver, changed, sizeof(changed), -EPROTONOSUPPORT);
	sign_message(key, epoch, 1, RK_AUTH_VERSION, 2, 2, 0, changed);
	expect_unchanged(&receiver, changed, sizeof(changed), -EOPNOTSUPP);
	sign_message(key, epoch, 1, RK_AUTH_VERSION, RK_AUTH_COMMAND_SET_LEVEL,
		     4, 0, changed);
	expect_unchanged(&receiver, changed, sizeof(changed), -EINVAL);
	sign_message(key, epoch, 1, RK_AUTH_VERSION, RK_AUTH_COMMAND_SET_LEVEL,
		     2, 1, changed);
	expect_unchanged(&receiver, changed, sizeof(changed), -EINVAL);
	sign_message(key, epoch, 0, RK_AUTH_VERSION, RK_AUTH_COMMAND_SET_LEVEL,
		     2, 0, changed);
	expect_unchanged(&receiver, changed, sizeof(changed), -EALREADY);
	sign_message(key, epoch, 2, RK_AUTH_VERSION, RK_AUTH_COMMAND_SET_LEVEL,
		     2, 0, changed);
	expect_unchanged(&receiver, changed, sizeof(changed), -ERANGE);

	rk_test_fail_at(3);
	expect_unchanged(&receiver, valid, sizeof(valid), -EIO);
	rk_test_fail_at(0);
	CHECK(rk_receiver_submit(&receiver, valid, sizeof(valid)) == 0);
	CHECK(rk_test_digest_was_cleared());
	CHECK(receiver.output_level == 2 && receiver.accepted == 1 &&
	      receiver.next_sequence == 2);
	expect_unchanged(&receiver, valid, sizeof(valid), -EALREADY);
	rk_receiver_stop(&receiver);
}

static void test_comparator_result_is_authoritative(void)
{
	struct rk_receiver receiver = {0};
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];
	u8 message[RK_AUTH_MESSAGE_LENGTH];
	u64 epoch = 0x5265706c61795361ULL;

	fill_keys(key, wrong);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), epoch, 1) == 0);
	sign_message(key, epoch, 1, RK_AUTH_VERSION,
		     RK_AUTH_COMMAND_SET_LEVEL, 2, 0, message);

	rk_test_force_tag_result(true, false);
	expect_unchanged(&receiver, message, sizeof(message), -EBADMSG);
	CHECK(rk_test_tag_comparisons() == 1);
	CHECK(rk_test_tag_length() == RK_AUTH_TAG_LENGTH);
	CHECK(rk_test_tag_bytes() == RK_AUTH_TAG_LENGTH);
	CHECK(rk_test_tag_compared_while_locked());

	message[RK_AUTH_BODY_LENGTH + 7] ^= 0x80;
	rk_test_force_tag_result(true, true);
	CHECK(rk_receiver_submit(&receiver, message, sizeof(message)) == 0);
	CHECK(rk_test_digest_was_cleared());
	CHECK(rk_test_tag_comparisons() == 1);
	CHECK(rk_test_tag_length() == RK_AUTH_TAG_LENGTH);
	CHECK(rk_test_tag_bytes() == RK_AUTH_TAG_LENGTH);
	CHECK(rk_test_tag_compared_while_locked());
	CHECK(receiver.accepted == 1 && receiver.next_sequence == 2 &&
	      receiver.output_level == 2);

	rk_test_force_tag_result(false, false);
	rk_receiver_stop(&receiver);
}

static void test_start_and_crypto_failures(void)
{
	struct rk_receiver receiver = {0};
	struct rk_receiver zero = {0};
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];

	fill_keys(key, wrong);
	CHECK(rk_receiver_submit(NULL, (const u8 *)"", 0) == -EINVAL);
	CHECK(rk_receiver_submit(&receiver, NULL, RK_AUTH_MESSAGE_LENGTH) ==
	      -EINVAL);
	CHECK(!memcmp(&receiver, &zero, sizeof(zero)));
	CHECK(rk_receiver_submit(&receiver, (const u8 *)"", 0) == -EMSGSIZE);
	CHECK(!memcmp(&receiver, &zero, sizeof(zero)));
	CHECK(rk_receiver_start(NULL, key, sizeof(key), 1, 1) == -EINVAL);
	CHECK(rk_receiver_start(&receiver, NULL, sizeof(key), 1, 1) == -EINVAL);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key) - 1, 1, 1) ==
	      -EINVAL);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), 0, 1) == -EINVAL);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), 1, 0) == -EINVAL);
	CHECK(!memcmp(&receiver, &zero, sizeof(zero)));

	rk_test_fail_at(1);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), 1, 1) == -ENOMEM);
	CHECK(!memcmp(&receiver, &zero, sizeof(zero)));
	rk_test_fail_at(2);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), 1, 1) ==
	      -EKEYREJECTED);
	CHECK(!memcmp(&receiver, &zero, sizeof(zero)));
	rk_test_fail_at(0);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), 1, 1) == 0);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), 1, 1) == -EBUSY);
	rk_receiver_stop(&receiver);
	CHECK(rk_test_live_contexts() == 0);
	CHECK(rk_test_erased_contexts() > 0);
	CHECK(!memcmp(&receiver, &zero, sizeof(zero)));
	rk_receiver_stop(&receiver);
	rk_receiver_stop(NULL);
}

static void test_sequences_and_restart(void)
{
	struct rk_receiver receiver = {0};
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];
	u8 message[RK_AUTH_MESSAGE_LENGTH];
	u64 epoch = 0x0101010101010101ULL;
	u64 sequence;

	fill_keys(key, wrong);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), epoch, 1) == 0);
	for (sequence = 1; sequence <= 512; sequence++) {
		sign_message(key, epoch, sequence, RK_AUTH_VERSION,
			     RK_AUTH_COMMAND_SET_LEVEL, (u8)(sequence & 3), 0,
			     message);
		CHECK(rk_receiver_submit(&receiver, message, sizeof(message)) ==
		      0);
		CHECK(rk_test_digest_was_cleared());
		CHECK(receiver.accepted == sequence);
		CHECK(receiver.next_sequence == sequence + 1);
		CHECK(receiver.output_level == (sequence & 3));
	}
	rk_receiver_stop(&receiver);

	sign_message(key, epoch, 1, RK_AUTH_VERSION, RK_AUTH_COMMAND_SET_LEVEL,
		     3, 0, message);
	CHECK(rk_receiver_start(&receiver, key, sizeof(key), epoch, 1) == 0);
	CHECK(rk_receiver_submit(&receiver, message, sizeof(message)) == 0);
	CHECK(rk_test_digest_was_cleared());
	CHECK(receiver.accepted == 1);
	rk_receiver_stop(&receiver);

	CHECK(rk_receiver_start(&receiver, key, sizeof(key), epoch,
				UINT64_MAX) == 0);
	sign_message(key, epoch, UINT64_MAX, RK_AUTH_VERSION,
		     RK_AUTH_COMMAND_SET_LEVEL, 1, 0, message);
	CHECK(rk_receiver_submit(&receiver, message, sizeof(message)) == 0);
	CHECK(rk_test_digest_was_cleared());
	CHECK(receiver.exhausted && receiver.next_sequence == UINT64_MAX);
	expect_unchanged(&receiver, message, sizeof(message), -EOVERFLOW);
	sign_message(key, epoch, 0, RK_AUTH_VERSION, RK_AUTH_COMMAND_SET_LEVEL,
		     1, 0, message);
	expect_unchanged(&receiver, message, sizeof(message), -EOVERFLOW);
	rk_receiver_stop(&receiver);
}

struct concurrent_case {
	struct rk_receiver *receiver;
	const u8 *message;
	pthread_barrier_t *barrier;
	int result;
	bool digest_cleared;
};

static void *submit_duplicate(void *argument)
{
	struct concurrent_case *test = argument;

	pthread_barrier_wait(test->barrier);
	test->result = rk_receiver_submit(test->receiver, test->message,
					  RK_AUTH_MESSAGE_LENGTH);
	test->digest_cleared = rk_test_digest_was_cleared();
	return NULL;
}

static void test_concurrent_duplicates(void)
{
	struct concurrent_case cases[THREADS];
	pthread_t threads[THREADS];
	pthread_barrier_t barrier;
	struct rk_receiver receiver;
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 wrong[RK_AUTH_KEY_LENGTH];
	u8 message[RK_AUTH_MESSAGE_LENGTH];
	unsigned int round;
	unsigned int index;

	fill_keys(key, wrong);
	for (round = 0; round < ROUNDS; round++) {
		unsigned int accepted = 0;
		unsigned int replayed = 0;

		memset(&receiver, 0, sizeof(receiver));
		CHECK(rk_receiver_start(&receiver, key, sizeof(key),
					0x9000000000000000ULL + round, 1) == 0);
		sign_message(key, receiver.epoch, 1, RK_AUTH_VERSION,
			     RK_AUTH_COMMAND_SET_LEVEL, 3, 0, message);
		CHECK(pthread_barrier_init(&barrier, NULL, THREADS) == 0);
		for (index = 0; index < THREADS; index++) {
			cases[index].receiver = &receiver;
			cases[index].message = message;
			cases[index].barrier = &barrier;
			cases[index].result = -999;
			cases[index].digest_cleared = false;
			CHECK(pthread_create(&threads[index], NULL,
					     submit_duplicate,
					     &cases[index]) == 0);
		}
		for (index = 0; index < THREADS; index++) {
			CHECK(pthread_join(threads[index], NULL) == 0);
			if (!cases[index].result)
				accepted++;
			else if (cases[index].result == -EALREADY)
				replayed++;
			CHECK(cases[index].digest_cleared);
		}
		CHECK(pthread_barrier_destroy(&barrier) == 0);
		CHECK(accepted == 1);
		CHECK(replayed == THREADS - 1);
		CHECK(receiver.accepted == 1 && receiver.next_sequence == 2 &&
		      receiver.output_level == 3);
		rk_receiver_stop(&receiver);
	}
	CHECK(rk_test_live_contexts() == 0);
}

int main(void)
{
	test_rfc4231_oracle();
	test_known_answer_and_key_ownership();
	test_start_and_crypto_failures();
	test_fail_closed_matrix();
	test_comparator_result_is_authoritative();
	test_sequences_and_restart();
	test_concurrent_duplicates();
	printf("[+] authenticated replay-safe message: %lu assertions\n",
	       assertions);
	return EXIT_SUCCESS;
}
