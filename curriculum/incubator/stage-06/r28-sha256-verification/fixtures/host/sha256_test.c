// SPDX-License-Identifier: GPL-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

struct crypto_shash { unsigned int marker; };

enum fake_failure {
	FAIL_NONE,
	FAIL_TFM_ERROR,
	FAIL_TFM_NULL,
	FAIL_DESCRIPTOR,
	FAIL_DIGEST,
	FAIL_DIGEST_POSITIVE,
};

struct fake_state {
	struct crypto_shash tfm;
	enum fake_failure failure;
	size_t digest_size;
	size_t descriptor_size;
	size_t allocation_size;
	unsigned long allocations;
	unsigned long descriptor_frees;
	unsigned long tfm_allocations;
	unsigned long tfm_frees;
	unsigned long digests;
	unsigned long comparisons;
	unsigned long compared_bytes;
	bool override_comparison;
	int comparison_result;
	int algorithm_ok;
};

static unsigned long assertions;
static char error_token;

static void expect(int condition, const char *message)
{
	assertions++;
	if (condition)
		return;
	fprintf(stderr, "[x] assertion %lu failed: %s\n", assertions, message);
	exit(EXIT_FAILURE);
}

static void fake_hash(const unsigned char *data, size_t length,
		      unsigned char output[RK_SHA256_DIGEST_SIZE])
{
	size_t index;

	for (index = 0; index < RK_SHA256_DIGEST_SIZE; index++)
		output[index] = (unsigned char)(index * 29U + length * 17U);
	for (index = 0; index < length; index++) {
		output[index % RK_SHA256_DIGEST_SIZE] ^=
			(unsigned char)(data[index] + index * 13U);
		output[(index * 7U) % RK_SHA256_DIGEST_SIZE] += data[index];
	}
}

static void reset_state(struct fake_state *state)
{
	memset(state, 0, sizeof(*state));
	state->tfm.marker = 0x53484132U;
	state->digest_size = RK_SHA256_DIGEST_SIZE;
	state->descriptor_size = 73;
}

static struct crypto_shash *fake_alloc(const char *name, unsigned int type,
				       unsigned int mask, void *context)
{
	struct fake_state *state = context;

	state->tfm_allocations++;
	state->algorithm_ok = !strcmp(name, "sha256") && !type && !mask;
	if (state->failure == FAIL_TFM_ERROR)
		return (struct crypto_shash *)&error_token;
	if (state->failure == FAIL_TFM_NULL)
		return NULL;
	return &state->tfm;
}

static bool fake_is_error(const void *pointer, void *context)
{
	(void)context;
	return pointer == &error_token;
}

static int fake_pointer_error(const void *pointer, void *context)
{
	(void)context;
	return pointer == &error_token ? -ENOENT : 0;
}

static size_t fake_digest_size(struct crypto_shash *tfm, void *context)
{
	struct fake_state *state = context;

	expect(tfm == &state->tfm, "digest size receives the allocated transform");
	return state->digest_size;
}

static size_t fake_descriptor_size(struct crypto_shash *tfm, void *context)
{
	struct fake_state *state = context;

	expect(tfm == &state->tfm, "descriptor size receives the transform");
	return state->descriptor_size;
}

static struct shash_desc *fake_allocate(size_t size, void *context)
{
	struct fake_state *state = context;

	state->allocations++;
	state->allocation_size = size;
	if (state->failure == FAIL_DESCRIPTOR || size > 4096)
		return NULL;
	return malloc(size);
}

static void fake_deallocate(struct shash_desc *desc, void *context)
{
	struct fake_state *state = context;

	state->descriptor_frees++;
	free(desc);
}

static int fake_digest(struct shash_desc *desc, const unsigned char *data,
		       unsigned int length, unsigned char *output, void *context)
{
	struct fake_state *state = context;

	state->digests++;
	expect(desc->tfm == &state->tfm,
	       "descriptor is bound to the allocated transform");
	fake_hash(data, length, output);
	if (state->failure == FAIL_DIGEST) {
		memset(output, 0x6d, RK_SHA256_DIGEST_SIZE);
		return -EIO;
	}
	if (state->failure == FAIL_DIGEST_POSITIVE)
		return 1;
	return 0;
}

static void fake_free_tfm(struct crypto_shash *tfm, void *context)
{
	struct fake_state *state = context;

	expect(tfm == &state->tfm, "free receives the allocated transform");
	state->tfm_frees++;
}

static int fake_memneq(const void *left, const void *right, size_t length,
		       void *context)
{
	const unsigned char *a = left;
	const unsigned char *b = right;
	struct fake_state *state = context;
	unsigned char difference = 0;
	size_t index;

	state->comparisons++;
	for (index = 0; index < length; index++) {
		difference |= a[index] ^ b[index];
		state->compared_bytes++;
	}
	if (state->override_comparison)
		return state->comparison_result;
	return difference != 0;
}

static const struct rk_sha256_api fake_api = {
	.crypto_alloc_shash = fake_alloc,
	.is_error = fake_is_error,
	.pointer_error = fake_pointer_error,
	.crypto_shash_digestsize = fake_digest_size,
	.crypto_shash_descsize = fake_descriptor_size,
	.allocate_descriptor = fake_allocate,
	.free_descriptor = fake_deallocate,
	.crypto_shash_digest = fake_digest,
	.crypto_free_shash = fake_free_tfm,
	.crypto_memneq = fake_memneq,
};

static void fill(unsigned char *bytes, size_t length, unsigned int salt)
{
	size_t index;

	for (index = 0; index < length; index++)
		bytes[index] = (unsigned char)(index * 131U + salt * 19U);
}

static void check_success_boundaries(void)
{
	unsigned char input[513];
	unsigned char wanted[RK_SHA256_DIGEST_SIZE];
	unsigned char output[RK_SHA256_DIGEST_SIZE + 8];
	struct fake_state state;
	size_t length;
	bool matches;

	for (length = 0; length <= sizeof(input); length++) {
		fill(input, length, (unsigned int)length);
		fake_hash(input, length, wanted);
		reset_state(&state);
		memset(output, 0xa5, sizeof(output));
		expect(!rk_sha256_digest_with(&fake_api, &state, input, length,
					      output),
		       "every bounded input length must hash");
		expect(!memcmp(output, wanted, sizeof(wanted)),
		       "committed digest must match the crypto operation");
		expect(!memcmp(output + RK_SHA256_DIGEST_SIZE,
			       (unsigned char[8]) { 0xa5, 0xa5, 0xa5, 0xa5,
						    0xa5, 0xa5, 0xa5, 0xa5 }, 8),
		       "digest stops after 32 bytes");
		expect(state.algorithm_ok && state.tfm_allocations == 1 &&
		       state.allocations == 1 && state.digests == 1 &&
		       state.descriptor_frees == 1 && state.tfm_frees == 1,
		       "successful lifecycle allocates and frees each object once");
		expect(state.allocation_size == sizeof(struct shash_desc) + 73,
		       "descriptor allocation includes header and context");

		reset_state(&state);
		matches = false;
		expect(!rk_sha256_verify_with(&fake_api, &state, input, length,
					      wanted, &matches) && matches,
		       "matching digest verifies");
		expect(state.comparisons == 1 &&
		       state.compared_bytes == RK_SHA256_DIGEST_SIZE,
		       "verification compares all digest bytes");
	}
}

static void expect_digest_failure(struct fake_state *state, int wanted,
				  const char *message)
{
	unsigned char input[4] = { 0x00, 0xff, 0x41, 0x80 };
	unsigned char output[RK_SHA256_DIGEST_SIZE + 8];
	unsigned char saved[sizeof(output)];

	memset(output, 0xa5, sizeof(output));
	memcpy(saved, output, sizeof(saved));
	expect(rk_sha256_digest_with(&fake_api, state, input, sizeof(input),
				     output) == wanted, message);
	expect(!memcmp(output, saved, sizeof(output)),
	       "digest failure leaves the complete output unchanged");
}

static void check_failures(void)
{
	unsigned char input[4] = { 0x00, 0xff, 0x41, 0x80 };
	unsigned char output[RK_SHA256_DIGEST_SIZE];
	unsigned char saved_output[RK_SHA256_DIGEST_SIZE];
	unsigned char expected[RK_SHA256_DIGEST_SIZE];
	struct rk_sha256_api incomplete;
	struct fake_state state;
	bool matches;

	reset_state(&state);
	memset(output, 0xa5, sizeof(output));
	memcpy(saved_output, output, sizeof(saved_output));
	expect(rk_sha256_digest_with(NULL, &state, input, sizeof(input), output) ==
	       -EINVAL, "missing API is rejected");
	expect(rk_sha256_digest_with(&fake_api, &state, NULL, 0, output) ==
	       -EINVAL, "missing data is rejected even when empty");
	expect(rk_sha256_digest_with(&fake_api, &state, input, sizeof(input),
				     NULL) == -EINVAL,
	       "missing digest output is rejected");

#define CHECK_MISSING(field) do { \
	incomplete = fake_api; \
	incomplete.field = NULL; \
	expect(rk_sha256_digest_with(&incomplete, &state, input, sizeof(input), \
				     output) == -EINVAL, \
	       "incomplete API is rejected before use"); \
} while (0)
	CHECK_MISSING(crypto_alloc_shash);
	CHECK_MISSING(is_error);
	CHECK_MISSING(pointer_error);
	CHECK_MISSING(crypto_shash_digestsize);
	CHECK_MISSING(crypto_shash_descsize);
	CHECK_MISSING(allocate_descriptor);
	CHECK_MISSING(free_descriptor);
	CHECK_MISSING(crypto_shash_digest);
	CHECK_MISSING(crypto_free_shash);
#undef CHECK_MISSING
	expect(!memcmp(output, saved_output, sizeof(output)),
	       "invalid requests leave digest output unchanged");
	incomplete = fake_api;
	incomplete.crypto_memneq = NULL;
	reset_state(&state);
	expect(!rk_sha256_digest_with(&incomplete, &state, input,
				      sizeof(input), output),
	       "digest does not require a comparison operation");
	matches = true;
	expect(rk_sha256_verify_with(&incomplete, &state, input,
				     sizeof(input), output, &matches) == -EINVAL &&
	       matches,
	       "verification requires a comparison operation");
	memcpy(output, saved_output, sizeof(output));

#if SIZE_MAX > UINT_MAX
	reset_state(&state);
	expect(rk_sha256_digest_with(&fake_api, &state, input,
				     (size_t)UINT_MAX + 1, output) == -E2BIG &&
	       state.tfm_allocations == 0,
	       "length that cannot reach the kernel API is rejected early");
	expect(!memcmp(output, saved_output, sizeof(output)),
	       "oversized length leaves digest output unchanged");
#endif

	reset_state(&state);
	state.failure = FAIL_TFM_ERROR;
	expect_digest_failure(&state, -ENOENT, "transform error is preserved");
	expect(state.tfm_frees == 0 && state.allocations == 0,
	       "error pointer is never freed or used");
	reset_state(&state);
	state.failure = FAIL_TFM_NULL;
	expect_digest_failure(&state, -ENOMEM, "null transform fails closed");

	reset_state(&state);
	state.digest_size = 31;
	expect_digest_failure(&state, -EPROTO, "short digest algorithm is rejected");
	expect(state.tfm_frees == 1 && state.allocations == 0,
	       "digest-size mismatch frees only the transform");
	reset_state(&state);
	state.digest_size = 33;
	expect_digest_failure(&state, -EPROTO, "long digest algorithm is rejected");

	reset_state(&state);
	state.descriptor_size = SIZE_MAX;
	expect_digest_failure(&state, -EOVERFLOW,
			      "descriptor addition overflow is rejected");
	expect(state.allocations == 0 && state.tfm_frees == 1,
	       "overflow is detected before allocation");
	reset_state(&state);
	state.descriptor_size = SIZE_MAX - sizeof(struct shash_desc) + 1;
	expect_digest_failure(&state, -EOVERFLOW,
			      "first overflowing descriptor size is rejected");
	reset_state(&state);
	state.descriptor_size = SIZE_MAX - sizeof(struct shash_desc);
	expect_digest_failure(&state, -ENOMEM,
			      "largest non-overflowing allocation is attempted exactly");
	expect(state.allocation_size == SIZE_MAX,
	       "largest valid descriptor total does not wrap");

	reset_state(&state);
	state.failure = FAIL_DESCRIPTOR;
	expect_digest_failure(&state, -ENOMEM,
			      "descriptor allocation failure is reported");
	expect(state.tfm_frees == 1 && state.descriptor_frees == 0,
	       "unallocated descriptor is not freed");
	reset_state(&state);
	state.failure = FAIL_DIGEST;
	expect_digest_failure(&state, -EIO, "crypto digest error is preserved");
	expect(state.descriptor_frees == 1 && state.tfm_frees == 1,
	       "digest error releases both owned objects");
	reset_state(&state);
	state.failure = FAIL_DIGEST_POSITIVE;
	expect_digest_failure(&state, -EPROTO,
			      "impossible positive digest result fails closed");

	reset_state(&state);
	fake_hash(input, sizeof(input), expected);
	matches = true;
	state.failure = FAIL_DIGEST;
	expect(rk_sha256_verify_with(&fake_api, &state, input, sizeof(input),
				     expected, &matches) == -EIO && matches,
	       "verification failure leaves its result unchanged");
	expect(state.comparisons == 0,
	       "failed digest is never compared");
	reset_state(&state);
	matches = true;
	expect(rk_sha256_verify_with(&fake_api, &state, input, sizeof(input),
				     NULL, &matches) == -EINVAL && matches,
	       "missing expected digest leaves the result unchanged");
	expect(rk_sha256_verify_with(&fake_api, &state, input, sizeof(input),
				     expected, NULL) == -EINVAL,
	       "missing verification output is rejected");
}

static void check_descriptor_boundaries(void)
{
	static const size_t sizes[] = { 0, 1, 63, 64, 257 };
	unsigned char input = 0x42;
	unsigned char output[RK_SHA256_DIGEST_SIZE];
	struct fake_state state;
	size_t index;

	for (index = 0; index < sizeof(sizes) / sizeof(sizes[0]); index++) {
		reset_state(&state);
		state.descriptor_size = sizes[index];
		expect(!rk_sha256_digest_with(&fake_api, &state, &input, 1,
					      output),
		       "descriptor context boundary hashes successfully");
		expect(state.allocation_size ==
		       sizeof(struct shash_desc) + sizes[index],
		       "descriptor boundary allocation is exact");
	}
}

static void check_comparison_positions(void)
{
	unsigned char input[65];
	unsigned char expected[RK_SHA256_DIGEST_SIZE];
	unsigned char altered[RK_SHA256_DIGEST_SIZE];
	struct fake_state state;
	size_t index;
	bool matches;

	fill(input, sizeof(input), 7);
	fake_hash(input, sizeof(input), expected);
	for (index = 0; index < sizeof(expected); index++) {
		memcpy(altered, expected, sizeof(altered));
		altered[index] ^= 1;
		reset_state(&state);
		matches = true;
		expect(!rk_sha256_verify_with(&fake_api, &state, input,
					      sizeof(input), altered, &matches) &&
		       !matches, "mismatch at every position is rejected");
		expect(state.compared_bytes == RK_SHA256_DIGEST_SIZE,
		       "mismatch position does not shorten comparison work");
	}
}

static void check_comparison_result_is_authoritative(void)
{
	unsigned char input[65];
	unsigned char expected[RK_SHA256_DIGEST_SIZE];
	unsigned char altered[RK_SHA256_DIGEST_SIZE];
	struct fake_state state;
	bool matches;

	fill(input, sizeof(input), 11);
	fake_hash(input, sizeof(input), expected);
	memcpy(altered, expected, sizeof(altered));
	altered[17] ^= 0x80;

	reset_state(&state);
	state.override_comparison = true;
	state.comparison_result = 1;
	matches = true;
	expect(!rk_sha256_verify_with(&fake_api, &state, input, sizeof(input),
				      expected, &matches) && !matches,
	       "verification publishes the supplied comparator result");
	expect(state.comparisons == 1 &&
	       state.compared_bytes == RK_SHA256_DIGEST_SIZE,
	       "forced mismatch still compares all digest bytes");

	reset_state(&state);
	state.override_comparison = true;
	state.comparison_result = 0;
	matches = false;
	expect(!rk_sha256_verify_with(&fake_api, &state, input, sizeof(input),
				      altered, &matches) && matches,
	       "verification does not recompute the comparator decision");
}

int main(void)
{
	check_success_boundaries();
	check_failures();
	check_descriptor_boundaries();
	check_comparison_positions();
	check_comparison_result_is_authoritative();
	printf("[+] %lu SHA-256 lifecycle assertions passed\n", assertions);
	return EXIT_SUCCESS;
}
