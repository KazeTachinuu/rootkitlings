// SPDX-License-Identifier: GPL-2.0
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/string.h>

#include "authenticated.h"
#include "platform.h"
#include "selftest.h"

enum {
	RK_TEST_THREADS = 16,
};

struct rk_thread_case {
	struct rk_receiver *receiver;
	const u8 *message;
	struct completion *start;
	struct completion done;
	struct task_struct *task;
	int result;
};

static int rk_expect(bool condition, unsigned int *assertions)
{
	(*assertions)++;
	return condition ? 0 : -EINVAL;
}

#define RK_EXPECT(condition)                            \
	do {                                            \
		if (rk_expect((condition), assertions)) \
			return -EINVAL;                 \
	} while (0)

static void rk_write_be64(u8 *bytes, u64 value)
{
	int index;

	for (index = 7; index >= 0; index--) {
		bytes[index] = (u8)value;
		value >>= 8;
	}
}

static int rk_sign_message(const u8 key[RK_AUTH_KEY_LENGTH], u64 epoch,
			   u64 sequence, u8 command, u8 level, u8 reserved,
			   u8 message[RK_AUTH_MESSAGE_LENGTH])
{
	u8 digest[RK_AUTH_DIGEST_LENGTH] = {0};
	int error;

	memset(message, 0, RK_AUTH_MESSAGE_LENGTH);
	message[0] = RK_AUTH_VERSION;
	message[1] = command;
	rk_write_be64(message + 2, epoch);
	rk_write_be64(message + 10, sequence);
	message[18] = level;
	message[19] = reserved;
	error = rk_platform_sign_once(key, RK_AUTH_KEY_LENGTH, message,
				      RK_AUTH_BODY_LENGTH, digest);
	if (!error)
		memcpy(message + RK_AUTH_BODY_LENGTH, digest,
		       RK_AUTH_TAG_LENGTH);
	rk_platform_clear(digest, sizeof(digest));
	return error;
}

static int rk_expect_unchanged(struct rk_receiver *receiver, const u8 *message,
			       size_t length, int expected,
			       unsigned int *assertions)
{
	struct rk_receiver before = *receiver;
	int error = rk_receiver_submit(receiver, message, length);

	if (rk_expect(error == expected, assertions))
		return -EINVAL;
	return rk_expect(!memcmp(&before, receiver, sizeof(before)),
			 assertions);
}

static int rk_duplicate_thread(void *argument)
{
	struct rk_thread_case *test = argument;

	wait_for_completion(test->start);
	test->result = rk_receiver_submit(test->receiver, test->message,
					  RK_AUTH_MESSAGE_LENGTH);
	complete(&test->done);
	return 0;
}

static int rk_test_concurrent_duplicate(const u8 key[RK_AUTH_KEY_LENGTH],
					unsigned int *assertions)
{
	struct rk_thread_case cases[RK_TEST_THREADS];
	struct rk_receiver receiver = {0};
	struct completion start;
	u8 message[RK_AUTH_MESSAGE_LENGTH];
	unsigned int accepted = 0;
	unsigned int replayed = 0;
	unsigned int index;
	int error;

	error = rk_receiver_start(&receiver, key, RK_AUTH_KEY_LENGTH,
				  0x8877665544332211ULL, 1);
	if (error)
		return error;
	error = rk_sign_message(key, receiver.epoch, 1,
				RK_AUTH_COMMAND_SET_LEVEL, 2, 0, message);
	if (error)
		goto out_receiver;
	init_completion(&start);
	memset(cases, 0, sizeof(cases));
	for (index = 0; index < RK_TEST_THREADS; index++) {
		cases[index].receiver = &receiver;
		cases[index].message = message;
		cases[index].start = &start;
		init_completion(&cases[index].done);
		cases[index].task =
		    kthread_run(rk_duplicate_thread, &cases[index],
				"authenticated-message-duplicate-%u", index);
		if (IS_ERR(cases[index].task)) {
			error = PTR_ERR(cases[index].task);
			cases[index].task = NULL;
			goto out_threads;
		}
	}
	complete_all(&start);
	for (index = 0; index < RK_TEST_THREADS; index++)
		wait_for_completion(&cases[index].done);
	for (index = 0; index < RK_TEST_THREADS; index++) {
		if (cases[index].result == 0)
			accepted++;
		else if (cases[index].result == -EALREADY)
			replayed++;
	}
	RK_EXPECT(accepted == 1);
	RK_EXPECT(replayed == RK_TEST_THREADS - 1);
	RK_EXPECT(receiver.accepted == 1);
	RK_EXPECT(receiver.next_sequence == 2);
	RK_EXPECT(receiver.output_level == 2);
	error = 0;
out_threads:
	complete_all(&start);
	for (index = 0; index < RK_TEST_THREADS; index++)
		if (cases[index].task)
			kthread_stop(cases[index].task);
out_receiver:
	rk_receiver_stop(&receiver);
	rk_platform_clear(message, sizeof(message));
	return error;
}

int rk_authenticated_selftest(unsigned int *assertions)
{
	static const u8 expected_digest[RK_AUTH_DIGEST_LENGTH] = {
	    0xc7, 0x3d, 0x5c, 0xc1, 0x90, 0xde, 0xe2, 0x9b, 0x65, 0xdb, 0x60,
	    0x08, 0xaf, 0x47, 0x1d, 0x12, 0xd7, 0x09, 0x89, 0x35, 0x8f, 0x99,
	    0xc9, 0xe6, 0xfb, 0x49, 0xa4, 0x66, 0x14, 0x6d, 0x16, 0xf1,
	};
	struct rk_receiver receiver = {0};
	struct rk_receiver maximum = {0};
	u8 key[RK_AUTH_KEY_LENGTH];
	u8 signing_key[RK_AUTH_KEY_LENGTH];
	u8 wrong_key[RK_AUTH_KEY_LENGTH];
	u8 message[RK_AUTH_MESSAGE_LENGTH];
	u8 original[RK_AUTH_MESSAGE_LENGTH];
	u8 digest[RK_AUTH_DIGEST_LENGTH] = {0};
	u64 epoch = 0x0102030405060708ULL;
	size_t index;
	int error;

	if (!assertions)
		return -EINVAL;
	*assertions = 0;
	for (index = 0; index < sizeof(key); index++) {
		key[index] = (u8)index;
		signing_key[index] = (u8)index;
		wrong_key[index] = (u8)(index ^ 0xa5);
	}

#if ROOTKITLINGS_FAIL_POINT == 3
	/* Reach the receiver with a precomputed valid message; signing also
	 * fails. */
	memset(message, 0, sizeof(message));
	message[0] = RK_AUTH_VERSION;
	message[1] = RK_AUTH_COMMAND_SET_LEVEL;
	rk_write_be64(message + 2, epoch);
	rk_write_be64(message + 10, 1);
	message[18] = 3;
	memcpy(message + RK_AUTH_BODY_LENGTH, expected_digest,
	       RK_AUTH_TAG_LENGTH);
	error = rk_receiver_start(&receiver, key, sizeof(key), epoch, 1);
	if (error)
		goto out;
	{
		struct rk_receiver before = receiver;

		error = rk_receiver_submit(&receiver, message, sizeof(message));
		(*assertions)++;
		if (error != -EIO) {
			error = -EINVAL;
			goto out;
		}
		(*assertions)++;
		if (memcmp(&before, &receiver, sizeof(before))) {
			error = -EINVAL;
			goto out;
		}
	}
	error = -EIO;
	goto out;
#endif

	error = rk_sign_message(signing_key, epoch, 1,
				RK_AUTH_COMMAND_SET_LEVEL, 3, 0, message);
	if (error)
		goto out;
	error = rk_platform_sign_once(signing_key, sizeof(signing_key), message,
				      RK_AUTH_BODY_LENGTH, digest);
	if (error)
		goto out;
	RK_EXPECT(!memcmp(digest, expected_digest, sizeof(digest)));
	RK_EXPECT(!memcmp(message + RK_AUTH_BODY_LENGTH, expected_digest,
			  RK_AUTH_TAG_LENGTH));
	RK_EXPECT(message[18] == 3);

	RK_EXPECT(rk_receiver_start(NULL, key, sizeof(key), epoch, 1) ==
		  -EINVAL);
	RK_EXPECT(rk_receiver_submit(NULL, message, sizeof(message)) ==
		  -EINVAL);
	RK_EXPECT(!rk_expect_unchanged(&receiver, NULL, sizeof(message),
				       -EINVAL, assertions));
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -ENODEV, assertions));
	RK_EXPECT(rk_receiver_start(&receiver, key, sizeof(key) - 1, epoch,
				    1) == -EINVAL);
	RK_EXPECT(rk_receiver_start(&receiver, key, sizeof(key), 0, 1) ==
		  -EINVAL);
	RK_EXPECT(rk_receiver_start(&receiver, key, sizeof(key), epoch, 0) ==
		  -EINVAL);
	error = rk_receiver_start(&receiver, key, sizeof(key), epoch, 1);
	if (error)
		goto out;
	rk_platform_clear(key, sizeof(key));
	RK_EXPECT(rk_receiver_start(&receiver, signing_key, sizeof(signing_key),
				    epoch, 1) == -EBUSY);
	RK_EXPECT(rk_receiver_submit(&receiver, message,
				     RK_AUTH_MESSAGE_LENGTH) == 0);
	RK_EXPECT(receiver.output_level == 3);
	RK_EXPECT(receiver.accepted == 1);
	RK_EXPECT(receiver.next_sequence == 2);

	RK_EXPECT(!rk_sign_message(signing_key, epoch, 2,
				   RK_AUTH_COMMAND_SET_LEVEL, 1, 0, original));
	for (index = 0; index < RK_AUTH_MESSAGE_LENGTH; index++) {
		memcpy(message, original, sizeof(message));
		message[index] ^= 1;
		RK_EXPECT(!rk_expect_unchanged(
		    &receiver, message, sizeof(message), -EBADMSG, assertions));
	}
	for (index = 0; index < RK_AUTH_MESSAGE_LENGTH; index++)
		RK_EXPECT(!rk_expect_unchanged(&receiver, original, index,
					       -EMSGSIZE, assertions));
	for (index = RK_AUTH_MESSAGE_LENGTH + 1;
	     index <= RK_AUTH_MESSAGE_LENGTH + 4; index++)
		RK_EXPECT(!rk_expect_unchanged(&receiver, original, index,
					       -EMSGSIZE, assertions));

	RK_EXPECT(!rk_sign_message(wrong_key, epoch, 2,
				   RK_AUTH_COMMAND_SET_LEVEL, 1, 0, message));
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -EBADMSG, assertions));
	RK_EXPECT(!rk_sign_message(signing_key, epoch + 1, 2,
				   RK_AUTH_COMMAND_SET_LEVEL, 1, 0, message));
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -ESTALE, assertions));
	RK_EXPECT(!rk_sign_message(signing_key, epoch, 1,
				   RK_AUTH_COMMAND_SET_LEVEL, 1, 0, message));
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -EALREADY, assertions));
	RK_EXPECT(!rk_sign_message(signing_key, epoch, 3,
				   RK_AUTH_COMMAND_SET_LEVEL, 1, 0, message));
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -ERANGE, assertions));

	RK_EXPECT(!rk_sign_message(signing_key, epoch, 2, 2, 1, 0, message));
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -EOPNOTSUPP, assertions));
	RK_EXPECT(!rk_sign_message(signing_key, epoch, 2,
				   RK_AUTH_COMMAND_SET_LEVEL, 4, 0, message));
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -EINVAL, assertions));
	RK_EXPECT(!rk_sign_message(signing_key, epoch, 2,
				   RK_AUTH_COMMAND_SET_LEVEL, 1, 1, message));
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -EINVAL, assertions));
	RK_EXPECT(!rk_sign_message(signing_key, epoch, 2,
				   RK_AUTH_COMMAND_SET_LEVEL, 1, 0, message));
	message[0] = 2;
	RK_EXPECT(!rk_platform_sign_once(signing_key, sizeof(signing_key),
					 message, RK_AUTH_BODY_LENGTH, digest));
	memcpy(message + RK_AUTH_BODY_LENGTH, digest, RK_AUTH_TAG_LENGTH);
	RK_EXPECT(!rk_expect_unchanged(&receiver, message, sizeof(message),
				       -EPROTONOSUPPORT, assertions));

	RK_EXPECT(!rk_sign_message(signing_key, epoch, 2,
				   RK_AUTH_COMMAND_SET_LEVEL, 1, 0, message));
	RK_EXPECT(rk_receiver_submit(&receiver, message, sizeof(message)) == 0);
	RK_EXPECT(receiver.output_level == 1);
	RK_EXPECT(receiver.accepted == 2);
	RK_EXPECT(receiver.next_sequence == 3);

	error = rk_receiver_start(&maximum, signing_key, sizeof(signing_key),
				  epoch, ~(u64)0);
	if (error)
		goto out;
	RK_EXPECT(!rk_sign_message(signing_key, epoch, ~(u64)0,
				   RK_AUTH_COMMAND_SET_LEVEL, 2, 0, message));
	RK_EXPECT(rk_receiver_submit(&maximum, message, sizeof(message)) == 0);
	RK_EXPECT(maximum.exhausted);
	RK_EXPECT(maximum.next_sequence == ~(u64)0);
	RK_EXPECT(maximum.accepted == 1);
	RK_EXPECT(!rk_expect_unchanged(&maximum, message, sizeof(message),
				       -EOVERFLOW, assertions));
	rk_receiver_stop(&maximum);

	rk_receiver_stop(&receiver);
	RK_EXPECT(!rk_sign_message(signing_key, epoch, 1,
				   RK_AUTH_COMMAND_SET_LEVEL, 3, 0, message));
	RK_EXPECT(rk_receiver_start(&receiver, signing_key, sizeof(signing_key),
				    epoch, 1) == 0);
	RK_EXPECT(rk_receiver_submit(&receiver, message, sizeof(message)) == 0);
	RK_EXPECT(receiver.accepted == 1);
	rk_receiver_stop(&receiver);

	error = rk_test_concurrent_duplicate(signing_key, assertions);
out:
	rk_receiver_stop(&maximum);
	rk_receiver_stop(&receiver);
	rk_platform_clear(key, sizeof(key));
	rk_platform_clear(signing_key, sizeof(signing_key));
	rk_platform_clear(wrong_key, sizeof(wrong_key));
	rk_platform_clear(message, sizeof(message));
	rk_platform_clear(original, sizeof(original));
	rk_platform_clear(digest, sizeof(digest));
	return error;
}
