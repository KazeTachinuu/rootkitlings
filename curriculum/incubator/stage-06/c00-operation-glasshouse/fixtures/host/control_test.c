#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "secure_control.h"

void rk_test_crypto_fail(int error);
void rk_test_clear_reset(void);
unsigned int rk_test_clear_calls(void);
unsigned int rk_test_clear_failures(void);
unsigned int rk_test_clear_length(size_t length);
int rk_test_make_envelope(const u8 *key, u8 kind, uint64_t session,
			  uint64_t sequence, const u8 *plaintext,
			  size_t plaintext_length, u8 *envelope,
			  size_t capacity, size_t *used);
int rk_test_make_envelope_version(const u8 *key, u8 version, u8 kind,
				  uint64_t session, uint64_t sequence,
				  const u8 *plaintext, size_t plaintext_length,
				  u8 *envelope, size_t capacity, size_t *used);

static unsigned long assertions;

#define CHECK(condition) do { \
	assertions++; \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void fill_keys(u8 key[RK_AEAD_KEY_LENGTH],
		      u8 wrong[RK_AEAD_KEY_LENGTH])
{
	for (size_t index = 0; index < RK_AEAD_KEY_LENGTH; index++) {
		key[index] = (u8)index;
		wrong[index] = (u8)(index ^ 0xa5);
	}
}

static int all_bytes_are(const u8 *bytes, size_t length, u8 value)
{
	for (size_t index = 0; index < length; index++)
		if (bytes[index] != value)
			return 0;
	return 1;
}

static void expect_open_unchanged(struct rk_secure_channel *channel,
				  const u8 *envelope, size_t length,
				  int expected)
{
	struct rk_secure_channel before = *channel;
	u8 output[RK_SECURE_PLAINTEXT_MAX];
	size_t output_length = 0x5555;
	int error;

	memset(output, 0xa5, sizeof(output));
	error = rk_secure_open_request(channel, envelope, length, output,
				       sizeof(output), &output_length);
	if (expected != 1 && error != expected)
		fprintf(stderr, "open length=%zu returned %d, expected %d\n",
			length, error, expected);
	CHECK(expected == 1 ? error != 0 : error == expected);
	CHECK(!memcmp(channel, &before, sizeof(before)));
	CHECK(output_length == 0x5555);
	for (size_t index = 0; index < sizeof(output); index++)
		CHECK(output[index] == 0xa5);
}

static void test_channel(void)
{
	static const u8 expected_request[] = {
		0x01,0x01,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0xd2,0xb0,
		0xfe,0x11,0xd6,0x62,0xc2,0xe0,0xb2,0xf9,0x1c,0xeb,
		0xb7,0x56,0x42,0xbb,0x74,0x15,0x07,0x92,
	};
	static const u8 expected_reply[] = {
		0x01,0x02,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x4a,0x1e,
		0xcf,0x31,0x35,0x6f,0x09,0xa8,0x03,0x93,0xf7,0xdd,
		0x55,0x40,0xdc,0x4a,0x1d,0x8a,0x06,0x84,
	};
	const uint64_t session = UINT64_C(0x0102030405060708);
	struct rk_secure_channel channel = { 0 };
	u8 key[RK_AEAD_KEY_LENGTH], wrong[RK_AEAD_KEY_LENGTH];
	u8 envelope[RK_SECURE_ENVELOPE_MAX];
	u8 changed[RK_SECURE_ENVELOPE_MAX];
	u8 output[RK_SECURE_PLAINTEXT_MAX];
	size_t used = 0, output_length = 0;

	fill_keys(key, wrong);
	CHECK(rk_secure_channel_start(&channel, key, sizeof(key), session) == 0);
	CHECK(channel.ready && channel.session == session &&
	      channel.next_request == 1 && channel.next_reply == 1);
	CHECK(rk_test_make_envelope(key, RK_SECURE_REQUEST, session, 1,
				    (const u8 *)"PING", 4, envelope,
				    sizeof(envelope), &used) == 0);
	CHECK(used == sizeof(expected_request));
	CHECK(!memcmp(envelope, expected_request, sizeof(expected_request)));
	rk_test_clear_reset();
	CHECK(rk_secure_open_request(&channel, envelope, used, output,
				     sizeof(output), &output_length) == 0);
	CHECK(rk_test_clear_calls() == 2 && rk_test_clear_failures() == 0);
	CHECK(rk_test_clear_length(RK_SECURE_PLAINTEXT_MAX) == 1);
	CHECK(rk_test_clear_length(RK_AEAD_NONCE_LENGTH) == 1);
	CHECK(output_length == 4 && !memcmp(output, "PING", 4));
	CHECK(channel.next_request == 2);
	expect_open_unchanged(&channel, envelope, used, -EALREADY);

	rk_test_clear_reset();
	CHECK(rk_secure_seal_reply(&channel, (const u8 *)"PONG", 4, envelope,
				   sizeof(envelope), &used) == 0);
	CHECK(rk_test_clear_calls() == 2 && rk_test_clear_failures() == 0);
	CHECK(rk_test_clear_length(RK_SECURE_ENVELOPE_MAX) == 1);
	CHECK(rk_test_clear_length(RK_AEAD_NONCE_LENGTH) == 1);
	CHECK(used == sizeof(expected_reply));
	CHECK(!memcmp(envelope, expected_reply, sizeof(expected_reply)));
	CHECK(channel.next_reply == 2);

	CHECK(rk_test_make_envelope(key, RK_SECURE_REQUEST, session, 3,
				    (const u8 *)"INFO", 4, envelope,
				    sizeof(envelope), &used) == 0);
	expect_open_unchanged(&channel, envelope, used, -ERANGE);
	CHECK(rk_test_make_envelope(key, RK_SECURE_REQUEST, session + 1, 2,
				    (const u8 *)"INFO", 4, envelope,
				    sizeof(envelope), &used) == 0);
	expect_open_unchanged(&channel, envelope, used, -ESTALE);
	CHECK(rk_test_make_envelope(key, RK_SECURE_REPLY, session, 2,
				    (const u8 *)"INFO", 4, envelope,
				    sizeof(envelope), &used) == 0);
	expect_open_unchanged(&channel, envelope, used, -EOPNOTSUPP);
	CHECK(rk_test_make_envelope_version(key, RK_SECURE_VERSION + 1,
				    RK_SECURE_REQUEST, session, 2,
				    (const u8 *)"INFO", 4, envelope,
				    sizeof(envelope), &used) == 0);
	expect_open_unchanged(&channel, envelope, used, -EPROTONOSUPPORT);
	CHECK(rk_test_make_envelope(wrong, RK_SECURE_REQUEST, session, 2,
				    (const u8 *)"INFO", 4, envelope,
				    sizeof(envelope), &used) == 0);
	expect_open_unchanged(&channel, envelope, used, -EBADMSG);
	CHECK(rk_test_make_envelope(key, RK_SECURE_REQUEST, session, 2,
				    (const u8 *)"INFO", 4, envelope,
				    sizeof(envelope), &used) == 0);
	for (size_t index = 0; index < used; index++) {
		memcpy(changed, envelope, used);
		changed[index] ^= 1;
		expect_open_unchanged(&channel, changed, used, 1);
	}
	for (size_t length = 0; length < used; length++) {
		int expected = length < RK_SECURE_HEADER_LENGTH +
			RK_AEAD_TAG_LENGTH ? -EMSGSIZE : -EBADMSG;

		expect_open_unchanged(&channel, envelope, length, expected);
	}

	rk_test_clear_reset();
	rk_test_crypto_fail(-EIO);
	expect_open_unchanged(&channel, envelope, used, -EIO);
	CHECK(rk_test_clear_calls() == 2 && rk_test_clear_failures() == 0);
	memset(changed, 0xa5, sizeof(changed));
	output_length = 0x4444;
	rk_test_clear_reset();
	rk_test_crypto_fail(-EIO);
	CHECK(rk_secure_seal_reply(&channel, (const u8 *)"ERR", 3, changed,
				   sizeof(changed), &output_length) == -EIO);
	CHECK(rk_test_clear_calls() == 2 && rk_test_clear_failures() == 0);
	CHECK(output_length == 0x4444 && channel.next_reply == 2);
	for (size_t index = 0; index < sizeof(changed); index++)
		CHECK(changed[index] == 0xa5);

	CHECK(rk_test_make_envelope(key, RK_SECURE_REQUEST, session, 2,
				    (const u8 *)"INFO", 4, envelope,
				    sizeof(envelope), &used) == 0);
	CHECK(rk_secure_open_request(&channel, envelope, used, output,
				     sizeof(output), &output_length) == 0);
	CHECK(output_length == 4 && !memcmp(output, "INFO", 4));

	channel.next_request = RK_SECURE_SEQUENCE_MAX;
	CHECK(rk_test_make_envelope(key, RK_SECURE_REQUEST, session,
				    RK_SECURE_SEQUENCE_MAX,
				    (const u8 *)"QUIT", 4, envelope,
				    sizeof(envelope), &used) == 0);
	CHECK(rk_secure_open_request(&channel, envelope, used, output,
				     sizeof(output), &output_length) == 0);
	CHECK(channel.request_exhausted);
	expect_open_unchanged(&channel, envelope, used, -EOVERFLOW);

	channel.next_reply = RK_SECURE_SEQUENCE_MAX;
	memset(changed, 0xa5, sizeof(changed));
	output_length = 0x4444;
	CHECK(rk_secure_seal_reply(&channel, (const u8 *)"PONG", 4, changed,
				   sizeof(changed), &output_length) == 0);
	CHECK(channel.reply_exhausted &&
	      channel.next_reply == RK_SECURE_SEQUENCE_MAX);
	memset(changed, 0xa5, sizeof(changed));
	output_length = 0x4444;
	CHECK(rk_secure_seal_reply(&channel, (const u8 *)"PONG", 4, changed,
				   sizeof(changed), &output_length) == -EOVERFLOW);
	CHECK(output_length == 0x4444);
	for (size_t index = 0; index < sizeof(changed); index++)
		CHECK(changed[index] == 0xa5);

	rk_test_clear_reset();
	rk_secure_channel_stop(&channel);
	CHECK(rk_test_clear_calls() == 1 && rk_test_clear_failures() == 0);
	CHECK(rk_test_clear_length(sizeof(channel)) == 1);
	CHECK(!channel.ready && channel.session == 0 && channel.key[0] == 0 &&
	      channel.key[RK_AEAD_KEY_LENGTH - 1] == 0);
}

static void test_validation(void)
{
	struct rk_secure_channel channel = { 0 };
	struct rk_secure_channel before;
	u8 key[RK_AEAD_KEY_LENGTH] = { 0 };
	u8 bytes[RK_SECURE_ENVELOPE_MAX];
	u8 envelope[RK_SECURE_ENVELOPE_MAX];
	size_t length = 9;
	size_t used = 0;

	memset(&channel, 0x5a, sizeof(channel));
	channel.ready = false;
	before = channel;
	CHECK(rk_secure_channel_start(NULL, key, sizeof(key), 1) == -EINVAL);
	CHECK(rk_secure_channel_start(&channel, NULL, sizeof(key), 1) == -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)));
	CHECK(rk_secure_channel_start(&channel, key, sizeof(key) - 1, 1) ==
	      -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)));
	CHECK(rk_secure_channel_start(&channel, key, sizeof(key) + 1, 1) ==
	      -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)));
	CHECK(rk_secure_channel_start(&channel, key, sizeof(key), 0) == -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)));
	CHECK(rk_secure_channel_start(&channel, key, sizeof(key), 1) == 0);
	before = channel;
	memset(bytes, 0xa5, sizeof(bytes));
	length = 9;
	CHECK(rk_secure_channel_start(&channel, key, sizeof(key), 2) == -EBUSY);
	CHECK(!memcmp(&channel, &before, sizeof(channel)));
	CHECK(rk_secure_open_request(NULL, bytes, sizeof(bytes), bytes,
				     sizeof(bytes), &length) == -EINVAL);
	CHECK(length == 9 && all_bytes_are(bytes, sizeof(bytes), 0xa5));
	CHECK(rk_secure_open_request(&channel, NULL, sizeof(bytes), bytes,
				     sizeof(bytes), &length) == -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)) && length == 9 &&
	      all_bytes_are(bytes, sizeof(bytes), 0xa5));
	CHECK(rk_secure_open_request(&channel, bytes, sizeof(bytes), NULL,
				     sizeof(bytes), &length) == -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)) && length == 9 &&
	      all_bytes_are(bytes, sizeof(bytes), 0xa5));
	CHECK(rk_secure_open_request(&channel, bytes, sizeof(bytes), bytes,
				     sizeof(bytes), NULL) == -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)) &&
	      all_bytes_are(bytes, sizeof(bytes), 0xa5));
	CHECK(rk_secure_seal_reply(NULL, bytes, 1, bytes, sizeof(bytes),
				   &length) == -EINVAL);
	CHECK(length == 9 && all_bytes_are(bytes, sizeof(bytes), 0xa5));
	CHECK(rk_secure_seal_reply(&channel, NULL, 1, bytes, sizeof(bytes),
				   &length) == -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)) && length == 9 &&
	      all_bytes_are(bytes, sizeof(bytes), 0xa5));
	CHECK(rk_secure_seal_reply(&channel, bytes, 1, NULL, sizeof(bytes),
				   &length) == -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)) && length == 9 &&
	      all_bytes_are(bytes, sizeof(bytes), 0xa5));
	CHECK(rk_secure_seal_reply(&channel, bytes, 1, bytes, sizeof(bytes),
				   NULL) == -EINVAL);
	CHECK(!memcmp(&channel, &before, sizeof(channel)) &&
	      all_bytes_are(bytes, sizeof(bytes), 0xa5));
	before = channel;
	memset(bytes, 0xa5, sizeof(bytes));
	length = 9;
	CHECK(rk_secure_open_request(&channel, bytes, sizeof(bytes) + 1, bytes,
				     sizeof(bytes), &length) == -EMSGSIZE);
	CHECK(length == 9 && !memcmp(&channel, &before, sizeof(channel)));
	CHECK(rk_test_make_envelope(key, RK_SECURE_REQUEST, 1, 1,
				    (const u8 *)"X", 1, envelope,
				    sizeof(envelope), &used) == 0);
	CHECK(rk_secure_open_request(&channel, envelope, used, bytes, 0,
				     &length) == -ENOSPC);
	CHECK(length == 9 && !memcmp(&channel, &before, sizeof(channel)));
	CHECK(rk_secure_seal_reply(&channel, bytes,
				   RK_SECURE_PLAINTEXT_MAX + 1, bytes,
				   sizeof(bytes), &length) == -EMSGSIZE);
	CHECK(length == 9 && !memcmp(&channel, &before, sizeof(channel)));
	CHECK(rk_secure_seal_reply(&channel, bytes, 1, bytes,
				   RK_SECURE_HEADER_LENGTH + RK_AEAD_TAG_LENGTH,
				   &length) == -ENOSPC);
	CHECK(length == 9 && !memcmp(&channel, &before, sizeof(channel)));
	rk_secure_channel_stop(&channel);
	rk_secure_channel_stop(NULL);
}

int main(void)
{
	test_channel();
	test_validation();
	printf("[+] authenticated control envelope: %lu assertions\n",
	       assertions);
	return EXIT_SUCCESS;
}
