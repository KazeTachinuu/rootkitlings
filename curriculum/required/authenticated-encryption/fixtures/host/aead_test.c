#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aead.h"

void rk_test_reset(void);
void rk_test_fail(int);
void rk_test_async(int, int);
unsigned int rk_test_live(void);
unsigned int rk_test_clears(void);
unsigned int rk_test_clear_mask(void);
unsigned int rk_test_waits(void);
unsigned int rk_test_violations(void);

static unsigned int checks;
#define CHECK(condition) do { \
	checks++; \
	if (!(condition)) { \
		fprintf(stderr, "check %u failed: %s:%d: %s\n", checks, \
			__FILE__, __LINE__, #condition); \
		exit(1); \
	} \
} while (0)

static void expect_bytes(const u8 *bytes, size_t length, u8 value)
{
	for (size_t i = 0; i < length; i++)
		CHECK(bytes[i] == value);
}

static void expect_finished(unsigned int waits)
{
	CHECK(rk_test_live() == 0);
	CHECK(rk_test_clears() == 2);
	CHECK(rk_test_clear_mask() == 3);
	CHECK(rk_test_waits() == waits);
	CHECK(rk_test_violations() == 0);
}

static void expect_auth_failure(const u8 *key, const u8 *nonce,
				const u8 *aad, size_t aad_length,
				const u8 *sealed, size_t sealed_length)
{
	u8 output[RK_AEAD_PLAINTEXT_MAX];
	size_t length = 37;
	int error;

	memset(output, 0x5a, sizeof(output));
	rk_test_reset();
	error = rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad,
				  aad_length, sealed, sealed_length, output,
				  sizeof(output), &length);
	CHECK(error == -EBADMSG);
	CHECK(length == 37);
	expect_bytes(output, sizeof(output), 0x5a);
	expect_finished(1);
}

static void expect_missing_operation(struct rk_aead_ops broken,
				     const u8 *key, const u8 *nonce,
				     const u8 *aad, const u8 *plaintext)
{
	u8 output[32];
	size_t length = 23;

	memset(output, 0xa7, sizeof(output));
	rk_test_reset();
	CHECK(rk_aead_seal_with(&broken, key, nonce, aad, 1, plaintext, 1,
				output, sizeof(output), &length) == -EINVAL);
	CHECK(length == 23);
	expect_bytes(output, sizeof(output), 0xa7);
	CHECK(rk_test_live() == 0);
	CHECK(rk_test_clears() == 0);
	CHECK(rk_test_waits() == 0);
	CHECK(rk_test_violations() == 0);
}

#define MISSING(member) do { \
	struct rk_aead_ops broken = rk_kernel_aead_ops; \
	broken.member = NULL; \
	expect_missing_operation(broken, key, nonce, aad, plaintext); \
} while (0)

int main(void)
{
	u8 key[RK_AEAD_KEY_LENGTH], bad_key[RK_AEAD_KEY_LENGTH];
	u8 nonce[RK_AEAD_NONCE_LENGTH], bad_nonce[RK_AEAD_NONCE_LENGTH];
	u8 aad[RK_AEAD_AAD_MAX], bad_aad[RK_AEAD_AAD_MAX];
	u8 plaintext[RK_AEAD_PLAINTEXT_MAX], sealed[96], bad_sealed[96];
	u8 opened[80], overlap[192];
	size_t sealed_length, plaintext_length;
	int error;
	static const int async_modes[] = { -EINPROGRESS, -EBUSY };
	static const int failure_errors[] = {
		-ENOMEM, -ENOMEM, -EPROTO, -EKEYREJECTED, -EINVAL, -EPROTO,
		-ENOMEM,
	};

	for (size_t i = 0; i < sizeof(key); i++)
		key[i] = (u8)i;
	for (size_t i = 0; i < sizeof(nonce); i++)
		nonce[i] = (u8)(0xa0 + i);
	for (size_t i = 0; i < sizeof(aad); i++)
		aad[i] = (u8)(0x40 + i);
	for (size_t i = 0; i < sizeof(plaintext); i++)
		plaintext[i] = (u8)(3 * i);

	/* Every supported AAD and plaintext length, with exact output capacity. */
	for (size_t aad_length = 0; aad_length <= RK_AEAD_AAD_MAX;
	     aad_length++) {
		for (size_t length = 0; length <= RK_AEAD_PLAINTEXT_MAX;
		     length++) {
			rk_test_reset();
			memset(sealed, 0xcc, sizeof(sealed));
			sealed_length = 999;
			error = rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce,
					aad, aad_length, plaintext, length, sealed,
					length + RK_AEAD_TAG_LENGTH,
					&sealed_length);
			CHECK(error == 0);
			CHECK(sealed_length == length + RK_AEAD_TAG_LENGTH);
			expect_bytes(sealed + sealed_length,
				     sizeof(sealed) - sealed_length, 0xcc);
			expect_finished(1);

			rk_test_reset();
			memset(opened, 0xdd, sizeof(opened));
			plaintext_length = 888;
			error = rk_aead_open_with(&rk_kernel_aead_ops, key, nonce,
					aad, aad_length, sealed, sealed_length, opened,
					length, &plaintext_length);
			CHECK(error == 0);
			CHECK(plaintext_length == length);
			CHECK(memcmp(opened, plaintext, length) == 0);
			expect_bytes(opened + length, sizeof(opened) - length, 0xdd);
			expect_finished(1);

			memcpy(bad_sealed, sealed, sealed_length);
			bad_sealed[sealed_length - 1] ^= 1;
			expect_auth_failure(key, nonce, aad, aad_length, bad_sealed,
					    sealed_length);
		}
	}

	/* The fake defers work until wait for both Linux asynchronous statuses. */
	for (size_t mode = 0; mode < sizeof(async_modes) / sizeof(async_modes[0]);
	     mode++) {
		int initial = async_modes[mode];

		rk_test_reset();
		rk_test_async(initial, 0);
		sealed_length = 0;
		CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 4,
					plaintext, 8, sealed, sizeof(sealed),
					&sealed_length) == 0);
		CHECK(sealed_length == 24);
		expect_finished(1);

		rk_test_reset();
		rk_test_async(initial, 0);
		plaintext_length = 0;
		CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 4,
					sealed, sealed_length, opened, sizeof(opened),
					&plaintext_length) == 0);
		CHECK(plaintext_length == 8);
		CHECK(memcmp(opened, plaintext, 8) == 0);
		expect_finished(1);

		rk_test_reset();
		rk_test_async(initial, -EIO);
		memset(sealed, 0xa5, sizeof(sealed));
		sealed_length = 41;
		CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 4,
					plaintext, 8, sealed, sizeof(sealed),
					&sealed_length) == -EIO);
		CHECK(sealed_length == 41);
		expect_bytes(sealed, sizeof(sealed), 0xa5);
		expect_finished(1);
	}

	/* Make one valid sealed input for setup and authentication error tests. */
	rk_test_reset();
	sealed_length = 0;
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 32,
				plaintext, 64, sealed, sizeof(sealed),
				&sealed_length) == 0);
	expect_finished(1);

	for (int point = 1; point <= 7; point++) {
		rk_test_reset();
		rk_test_fail(point);
		memset(opened, 0xa5, sizeof(opened));
		plaintext_length = 19;
		error = rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 32,
					 sealed, sealed_length, opened,
					 sizeof(opened), &plaintext_length);
		CHECK(error == failure_errors[point - 1]);
		CHECK(plaintext_length == 19);
		expect_bytes(opened, sizeof(opened), 0xa5);
		expect_finished(0);

		rk_test_reset();
		rk_test_fail(point);
		memset(bad_sealed, 0xa6, sizeof(bad_sealed));
		plaintext_length = 29;
		error = rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 2,
					 plaintext, 5, bad_sealed,
					 sizeof(bad_sealed), &plaintext_length);
		CHECK(error == failure_errors[point - 1]);
		CHECK(plaintext_length == 29);
		expect_bytes(bad_sealed, sizeof(bad_sealed), 0xa6);
		expect_finished(0);
	}

	/* Every byte of every authenticated input influences acceptance. */
	for (size_t i = 0; i < sealed_length; i++) {
		memcpy(bad_sealed, sealed, sealed_length);
		bad_sealed[i] ^= 1;
		expect_auth_failure(key, nonce, aad, 32, bad_sealed, sealed_length);
	}
	for (size_t i = 0; i < sizeof(aad); i++) {
		memcpy(bad_aad, aad, sizeof(aad));
		bad_aad[i] ^= 1;
		expect_auth_failure(key, nonce, bad_aad, sizeof(bad_aad), sealed,
				    sealed_length);
	}
	for (size_t i = 0; i < sizeof(nonce); i++) {
		memcpy(bad_nonce, nonce, sizeof(nonce));
		bad_nonce[i] ^= 1;
		expect_auth_failure(key, bad_nonce, aad, sizeof(aad), sealed,
				    sealed_length);
	}
	for (size_t i = 0; i < sizeof(key); i++) {
		memcpy(bad_key, key, sizeof(key));
		bad_key[i] ^= 1;
		expect_auth_failure(bad_key, nonce, aad, sizeof(aad), sealed,
				    sealed_length);
	}

	/* Immediate and completed failures may modify only the private work area. */
	rk_test_reset();
	rk_test_async(-EIO, 0);
	memset(bad_sealed, 0x71, sizeof(bad_sealed));
	plaintext_length = 31;
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 2,
				plaintext, 4, bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EIO);
	CHECK(plaintext_length == 31);
	expect_bytes(bad_sealed, sizeof(bad_sealed), 0x71);
	expect_finished(1);

	rk_test_reset();
	rk_test_async(1, 0);
	memset(bad_sealed, 0x72, sizeof(bad_sealed));
	plaintext_length = 32;
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 2,
				plaintext, 4, bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EPROTO);
	CHECK(plaintext_length == 32);
	expect_bytes(bad_sealed, sizeof(bad_sealed), 0x72);
	expect_finished(1);

	/* Exact in-place and both directions of partial overlap. */
	for (int layout = 0; layout < 3; layout++) {
		u8 *source = layout == 2 ? overlap + 16 : overlap;
		u8 *destination = layout == 0 ? source :
			(layout == 1 ? source + 3 : overlap);

		memset(overlap, 0x83, sizeof(overlap));
		memcpy(source, plaintext, 64);
		rk_test_reset();
		sealed_length = 0;
		CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 8,
					source, 64, destination, 80,
					&sealed_length) == 0);
		expect_finished(1);
		rk_test_reset();
		plaintext_length = 0;
		CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 8,
					destination, sealed_length, source, 64,
					&plaintext_length) == 0);
		CHECK(plaintext_length == 64);
		CHECK(memcmp(source, plaintext, 64) == 0);
		expect_finished(1);
	}

	/* Bounds and null pointers fail before acquiring or publishing anything. */
	memset(bad_sealed, 0xa5, sizeof(bad_sealed));
	plaintext_length = 12;
	rk_test_reset();
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 33,
				plaintext, 1, bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EMSGSIZE);
	CHECK(plaintext_length == 12);
	expect_bytes(bad_sealed, sizeof(bad_sealed), 0xa5);
	CHECK(rk_test_live() == 0 && rk_test_clears() == 0 && rk_test_waits() == 0);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				sealed, 15, opened, sizeof(opened),
				&plaintext_length) == -EMSGSIZE);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				sealed, 81, opened, sizeof(opened),
				&plaintext_length) == -EMSGSIZE);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				plaintext, 65, bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EMSGSIZE);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				plaintext, 2, bad_sealed, 17,
				&plaintext_length) == -ENOSPC);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				sealed, 18, opened, 1,
				&plaintext_length) == -ENOSPC);

	CHECK(rk_aead_seal_with(NULL, key, nonce, aad, 0, plaintext, 1,
				bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, NULL, nonce, aad, 0,
				plaintext, 1, bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, NULL, aad, 0,
				plaintext, 1, bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, NULL, 0,
				plaintext, 1, bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 0, NULL,
				0, bad_sealed, sizeof(bad_sealed),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				plaintext, 1, NULL, sizeof(bad_sealed),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				plaintext, 1, bad_sealed, sizeof(bad_sealed),
				NULL) == -EINVAL);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, NULL, nonce, aad, 0,
				sealed, 16, opened, sizeof(opened),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, NULL, aad, 0,
				sealed, 16, opened, sizeof(opened),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, NULL, 0,
				sealed, 16, opened, sizeof(opened),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 0, NULL,
				16, opened, sizeof(opened),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				sealed, 16, NULL, sizeof(opened),
				&plaintext_length) == -EINVAL);
	CHECK(rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, 0,
				sealed, 16, opened, sizeof(opened), NULL) == -EINVAL);

	MISSING(alloc); MISSING(is_error); MISSING(pointer_error);
	MISSING(iv_size); MISSING(auth_size); MISSING(set_key);
	MISSING(set_auth_size); MISSING(request_alloc); MISSING(request_free);
	MISSING(tfm_free); MISSING(sg_init_one); MISSING(wait_init);
	MISSING(set_callback); MISSING(set_ad); MISSING(set_crypt);
	MISSING(encrypt); MISSING(decrypt); MISSING(wait_request); MISSING(clear);

	printf("[+] %u authenticated-encryption assertions passed\n", checks);
	return 0;
}
