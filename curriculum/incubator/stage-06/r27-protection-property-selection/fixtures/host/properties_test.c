// SPDX-License-Identifier: GPL-2.0
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protection.h"

static unsigned long assertions;

static void expect(int condition, const char *message)
{
	assertions++;
	if (condition)
		return;
	fprintf(stderr, "[x] assertion %lu failed: %s\n", assertions, message);
	exit(EXIT_FAILURE);
}

static enum rk_protection_plan
expected_plan(unsigned int needs, unsigned int conditions)
{
	if ((needs & ~RK_NEED_MASK) || (conditions & ~RK_HAVE_MASK))
		return RK_PROTECTION_UNAVAILABLE;
	if (!needs)
		return RK_PROTECTION_ENCODING;
	if (needs & RK_NEED_CONFIDENTIALITY) {
		if (!(conditions & RK_HAVE_SHARED_KEY) ||
		    !(conditions & RK_HAVE_UNIQUE_NONCE))
			return RK_PROTECTION_UNAVAILABLE;
		if (!(needs & RK_NEED_FRESHNESS))
			return RK_PROTECTION_AEAD;
		return conditions & RK_HAVE_REPLAY_STATE ?
			RK_PROTECTION_AEAD_REPLAY : RK_PROTECTION_UNAVAILABLE;
	}
	if (needs & RK_NEED_FRESHNESS)
		return (conditions & RK_HAVE_SHARED_KEY) &&
		       (conditions & RK_HAVE_REPLAY_STATE) ?
			RK_PROTECTION_HMAC_SHA256_REPLAY :
			RK_PROTECTION_UNAVAILABLE;
	if (needs & RK_NEED_ORIGIN)
		return conditions & RK_HAVE_SHARED_KEY ?
			RK_PROTECTION_HMAC_SHA256 : RK_PROTECTION_UNAVAILABLE;
	if (conditions & RK_HAVE_TRUSTED_DIGEST)
		return RK_PROTECTION_SHA256_TRUSTED_DIGEST;
	return conditions & RK_HAVE_SHARED_KEY ?
		RK_PROTECTION_HMAC_SHA256 : RK_PROTECTION_UNAVAILABLE;
}

static void check_selector(void)
{
	unsigned int conditions, needs;

	for (needs = 0; needs < 32; needs++) {
		for (conditions = 0; conditions < 32; conditions++)
			expect(rk_choose_protection(needs, conditions) ==
			       expected_plan(needs, conditions),
			       "property selection must match the decision table");
	}
	expect(rk_choose_protection(UINT_MAX, 0) ==
	       RK_PROTECTION_UNAVAILABLE, "unknown requirement bits fail closed");
	expect(rk_choose_protection(0, UINT_MAX) ==
	       RK_PROTECTION_UNAVAILABLE, "unknown condition bits fail closed");

	expect(rk_choose_protection(RK_NEED_CONFIDENTIALITY,
				    RK_HAVE_SHARED_KEY) ==
	       RK_PROTECTION_UNAVAILABLE, "confidentiality requires a nonce");
	expect(rk_choose_protection(RK_NEED_CONFIDENTIALITY,
				    RK_HAVE_SHARED_KEY |
				    RK_HAVE_UNIQUE_NONCE) ==
	       RK_PROTECTION_AEAD, "confidentiality selects AEAD");
	expect(rk_choose_protection(RK_NEED_FRESHNESS,
				    RK_HAVE_SHARED_KEY) ==
	       RK_PROTECTION_UNAVAILABLE, "freshness requires replay state");
	expect(rk_choose_protection(RK_NEED_INTEGRITY, 0) ==
	       RK_PROTECTION_UNAVAILABLE, "an untrusted digest is insufficient");
}

static void fill(unsigned char *bytes, size_t length, unsigned int salt)
{
	size_t index;

	for (index = 0; index < length; index++)
		bytes[index] = (unsigned char)(index * 131U + salt * 29U);
}

static void check_xor_boundaries(void)
{
	unsigned char expected[RK_XOR_DATA_MAX];
	unsigned char input[RK_XOR_DATA_MAX];
	unsigned char key[RK_XOR_KEY_MAX];
	unsigned char output[RK_XOR_DATA_MAX + 16];
	unsigned char workspace[RK_XOR_DATA_MAX + RK_XOR_KEY_MAX + 320];
	size_t index, key_length, length;

	for (length = 0; length <= RK_XOR_DATA_MAX; length++) {
		for (key_length = 1; key_length <= RK_XOR_KEY_MAX;
		     key_length++) {
			fill(input, length, (unsigned int)(length + key_length));
			fill(key, key_length, (unsigned int)(key_length + 7));
			for (index = 0; index < length; index++)
				expected[index] = input[index] ^ key[index % key_length];

			memset(output, 0xa5, sizeof(output));
			expect(rk_xor_repeat(input, length, key, key_length,
					     output, length) == RK_XOR_OK,
			       "separate XOR buffers must succeed");
			expect(!memcmp(output, expected, length),
			       "separate XOR bytes must match");
			expect(output[length] == 0xa5,
			       "XOR must stop at the requested length");

			memcpy(output, input, length);
			expect(rk_xor_repeat(output, length, key, key_length,
					     output, length) == RK_XOR_OK,
			       "in-place XOR must succeed");
			expect(!memcmp(output, expected, length),
			       "in-place XOR bytes must match");

			memset(workspace, 0x5c, sizeof(workspace));
			memcpy(workspace + 32, input, length);
			memcpy(workspace + RK_XOR_DATA_MAX + 96, key,
			       key_length);
			expect(rk_xor_repeat(workspace + 32, length,
					     workspace + RK_XOR_DATA_MAX + 96,
					     key_length, workspace + 33,
					     length) == RK_XOR_OK,
			       "partially overlapping data must succeed");
			expect(!memcmp(workspace + 33, expected, length),
			       "overlapping data must use the original input");
		}
	}

	fill(input, sizeof(input), 9);
	fill(key, sizeof(key), 11);
	memset(workspace, 0x77, sizeof(workspace));
	memcpy(workspace + 40, input, sizeof(input));
	memcpy(workspace + 300, key, sizeof(key));
	for (index = 0; index < sizeof(input); index++)
		expected[index] = input[index] ^ key[index % sizeof(key)];
	expect(rk_xor_repeat(workspace + 40, sizeof(input), workspace + 300,
			     sizeof(key), workspace + 299,
			     sizeof(input)) == RK_XOR_OK,
	       "destination may overlap the key");
	expect(!memcmp(workspace + 299, expected, sizeof(expected)),
	       "overlapping key must be snapshotted");
}

static void check_xor_overlap_matrix(void)
{
	static const size_t lengths[] = { 1, 2, 3, 31, 32, 33, 63, 64 };
	static const size_t key_lengths[] = { 1, 2, 3, 7, 31, 32 };
	unsigned char arena[192];
	unsigned char before[sizeof(arena)];
	unsigned char input_copy[64];
	unsigned char key_copy[RK_XOR_KEY_MAX];
	unsigned char expected[64];
	size_t input_offset, key_offset, output_offset;
	size_t length_index, key_length_index, index;

	/*
	 * Move the input and key across representative boundary offsets, then
	 * exercise every destination offset from 0 through 64.  This covers
	 * disjoint buffers, exact aliases, both overlap directions, and layouts
	 * where the destination intersects the input and key at once.
	 */
	for (length_index = 0;
	     length_index < sizeof(lengths) / sizeof(lengths[0]);
	     length_index++) {
		for (key_length_index = 0;
		     key_length_index < sizeof(key_lengths) / sizeof(key_lengths[0]);
		     key_length_index++) {
			for (input_offset = 0; input_offset <= 64;
			     input_offset += 8) {
				for (key_offset = 0; key_offset <= 64;
				     key_offset += 8) {
					for (output_offset = 0; output_offset <= 64;
					     output_offset++) {
						fill(arena, sizeof(arena),
						     (unsigned int)(length_index * 17 +
						     key_length_index * 5 +
						     input_offset + key_offset +
						     output_offset));
						memcpy(before, arena, sizeof(arena));
						memcpy(input_copy, arena + input_offset,
						       lengths[length_index]);
						memcpy(key_copy, arena + key_offset,
						       key_lengths[key_length_index]);
						for (index = 0; index < lengths[length_index];
						     index++)
							expected[index] = input_copy[index] ^
								key_copy[index %
								key_lengths[key_length_index]];

						expect(rk_xor_repeat(arena + input_offset,
							       lengths[length_index],
							       arena + key_offset,
							       key_lengths[key_length_index],
							       arena + output_offset,
							       lengths[length_index]) ==
						       RK_XOR_OK,
						       "every overlap geometry must succeed");
						expect(!memcmp(arena + output_offset, expected,
							       lengths[length_index]),
						       "overlap must use original input and key");
						expect(!memcmp(arena, before, output_offset),
						       "overlap must not write before output");
						expect(!memcmp(arena + output_offset +
							       lengths[length_index],
							       before + output_offset +
							       lengths[length_index],
							       sizeof(arena) - output_offset -
							       lengths[length_index]),
						       "overlap must not write after output");
					}
				}
			}
		}
	}
}

static void check_xor_failures(void)
{
	unsigned char arena[RK_XOR_DATA_MAX + RK_XOR_KEY_MAX + 8];
	unsigned char arena_saved[sizeof(arena)];
	unsigned char input[RK_XOR_DATA_MAX + 1];
	unsigned char key[RK_XOR_KEY_MAX + 1];
	unsigned char output[RK_XOR_DATA_MAX + 8];
	unsigned char saved[sizeof(output)];

	fill(input, sizeof(input), 3);
	fill(key, sizeof(key), 4);
	memset(output, 0xa5, sizeof(output));
	memcpy(saved, output, sizeof(output));

#define EXPECT_FAILURE(call, wanted, text) do { \
	expect((call) == (wanted), (text)); \
	expect(!memcmp(output, saved, sizeof(output)), \
	       "failure must leave the destination unchanged"); \
} while (0)

	EXPECT_FAILURE(rk_xor_repeat(NULL, 1, key, 1, output,
				     sizeof(output)), RK_XOR_INVALID_ARGUMENT,
		       "missing input is invalid");
	EXPECT_FAILURE(rk_xor_repeat(input, 1, NULL, 1, output,
				     sizeof(output)), RK_XOR_INVALID_ARGUMENT,
		       "missing key is invalid");
	EXPECT_FAILURE(rk_xor_repeat(input, 1, key, 1, NULL,
				     sizeof(output)), RK_XOR_INVALID_ARGUMENT,
		       "missing output is invalid");
	EXPECT_FAILURE(rk_xor_repeat(input, 1, key, 0, output,
				     sizeof(output)), RK_XOR_INVALID_ARGUMENT,
		       "empty key is invalid");
	EXPECT_FAILURE(rk_xor_repeat(input, RK_XOR_DATA_MAX + 1, key, 1,
				     output, sizeof(output)), RK_XOR_TOO_LONG,
		       "oversized input is rejected");
	EXPECT_FAILURE(rk_xor_repeat(input, 1, key, RK_XOR_KEY_MAX + 1,
				     output, sizeof(output)), RK_XOR_TOO_LONG,
		       "oversized key is rejected");
	EXPECT_FAILURE(rk_xor_repeat(input, 8, key, 1, output, 7),
		       RK_XOR_NO_SPACE, "short destination is rejected");
	EXPECT_FAILURE(rk_xor_repeat(input, SIZE_MAX, key, 1, output,
				     SIZE_MAX), RK_XOR_TOO_LONG,
		       "wrapped or extreme lengths are rejected");
	EXPECT_FAILURE(rk_xor_repeat(NULL, RK_XOR_DATA_MAX + 1, key,
				     RK_XOR_KEY_MAX + 1, output, 0),
		       RK_XOR_INVALID_ARGUMENT,
		       "missing pointer takes priority over every size error");
	EXPECT_FAILURE(rk_xor_repeat(input, RK_XOR_DATA_MAX + 1, key,
				     RK_XOR_KEY_MAX + 1, output, 0),
		       RK_XOR_TOO_LONG,
		       "declared maximum takes priority over capacity");
	EXPECT_FAILURE(rk_xor_repeat(input, 1, key, 0, output, 0),
		       RK_XOR_INVALID_ARGUMENT,
		       "empty key takes priority over capacity");
	EXPECT_FAILURE(rk_xor_repeat(NULL, 0, key, 1, output, 0),
		       RK_XOR_INVALID_ARGUMENT,
		       "empty input still requires the input pointer");
	EXPECT_FAILURE(rk_xor_repeat(input, 0, NULL, 1, output, 0),
		       RK_XOR_INVALID_ARGUMENT,
		       "empty input still requires the key pointer");
	EXPECT_FAILURE(rk_xor_repeat(input, 0, key, 1, NULL, 0),
		       RK_XOR_INVALID_ARGUMENT,
		       "empty input still requires the output pointer");
#undef EXPECT_FAILURE

	memset(output, 0xa5, sizeof(output));
	expect(rk_xor_repeat(input, 0, key, 1, output, 0) == RK_XOR_OK,
	       "empty input with valid pointers succeeds");
	expect(!memcmp(output, saved, sizeof(output)),
	       "empty input leaves the destination unchanged");

	fill(arena, sizeof(arena), 19);
	memcpy(arena_saved, arena, sizeof(arena));
	expect(rk_xor_repeat(arena, 32, arena + 16, 8, arena + 1, 31) ==
	       RK_XOR_NO_SPACE, "overlapped short destination is rejected");
	expect(!memcmp(arena, arena_saved, sizeof(arena)),
	       "overlapped failure leaves all aliased bytes unchanged");
}

static void check_xor_attack(void)
{
	static const unsigned char known[] = "type=ping;id=0001";
	static const unsigned char target[] = "type=info;id=7342";
	static const unsigned char key[] = { 0xd3, 0x21, 0x8a, 0x04 };
	unsigned char known_cipher[sizeof(known) - 1];
	unsigned char target_cipher[sizeof(target) - 1];
	unsigned char recovered_key[sizeof(key)];
	unsigned char recovered[sizeof(target) - 1];
	unsigned char tampered[sizeof(target) - 1];
	size_t index;

	expect(rk_xor_repeat(known, sizeof(known) - 1, key, sizeof(key),
			     known_cipher, sizeof(known_cipher)) == RK_XOR_OK,
	       "known message must encode");
	expect(rk_xor_repeat(target, sizeof(target) - 1, key, sizeof(key),
			     target_cipher, sizeof(target_cipher)) == RK_XOR_OK,
	       "target message must encode");
	for (index = 0; index < sizeof(key); index++)
		recovered_key[index] = known[index] ^ known_cipher[index];
	expect(!memcmp(recovered_key, key, sizeof(key)),
	       "known plaintext reveals the repeated key");
	expect(rk_xor_repeat(target_cipher, sizeof(target_cipher),
			     recovered_key, sizeof(recovered_key), recovered,
			     sizeof(recovered)) == RK_XOR_OK,
	       "recovered key must decode another message");
	expect(!memcmp(recovered, target, sizeof(recovered)),
	       "reused XOR key exposes the other plaintext");

	memcpy(tampered, target_cipher, sizeof(tampered));
	tampered[5] ^= 0x20;
	expect(rk_xor_repeat(tampered, sizeof(tampered), key, sizeof(key),
			     recovered, sizeof(recovered)) == RK_XOR_OK,
	       "tampered XOR bytes are not rejected");
	expect((recovered[5] ^ target[5]) == 0x20,
	       "a ciphertext bit flip controls the plaintext bit");
	for (index = 0; index < sizeof(recovered); index++) {
		if (index != 5)
			expect(recovered[index] == target[index],
			       "the attacker-selected flip is localized");
	}
}

int main(void)
{
	check_selector();
	check_xor_boundaries();
	check_xor_overlap_matrix();
	check_xor_failures();
	check_xor_attack();
	printf("[+] %lu protection-property assertions passed\n", assertions);
	return EXIT_SUCCESS;
}
