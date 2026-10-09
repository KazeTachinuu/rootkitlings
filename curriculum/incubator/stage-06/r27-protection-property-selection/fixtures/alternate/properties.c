// SPDX-License-Identifier: GPL-2.0
#include "protection.h"

enum rk_xor_result
rk_xor_repeat(const unsigned char *input, size_t length,
	      const unsigned char *key, size_t key_length,
	      unsigned char *output, size_t capacity)
{
	unsigned char input_copy[RK_XOR_DATA_MAX];
	unsigned char key_copy[RK_XOR_KEY_MAX];
	size_t remaining = length;
	size_t position = 0;

	if (input == NULL || key == NULL || output == NULL || key_length == 0)
		return RK_XOR_INVALID_ARGUMENT;
	if (length > sizeof(input_copy) || key_length > sizeof(key_copy))
		return RK_XOR_TOO_LONG;
	if (length > capacity)
		return RK_XOR_NO_SPACE;
	memcpy(input_copy, input, length);
	memcpy(key_copy, key, key_length);
	while (remaining--) {
		input_copy[position] ^= key_copy[position % key_length];
		position++;
	}
	memcpy(output, input_copy, length);
	return RK_XOR_OK;
}

enum rk_protection_plan
rk_choose_protection(unsigned int needs, unsigned int conditions)
{
	unsigned int has_key, has_nonce, has_replay;

	if (needs > RK_NEED_MASK || conditions > RK_HAVE_MASK)
		return RK_PROTECTION_UNAVAILABLE;
	if (needs == 0)
		return RK_PROTECTION_ENCODING;
	has_key = conditions & RK_HAVE_SHARED_KEY;
	has_nonce = conditions & RK_HAVE_UNIQUE_NONCE;
	has_replay = conditions & RK_HAVE_REPLAY_STATE;

	if (needs & RK_NEED_CONFIDENTIALITY) {
		if (!has_key || !has_nonce)
			return RK_PROTECTION_UNAVAILABLE;
		if (needs & RK_NEED_FRESHNESS)
			return has_replay ? RK_PROTECTION_AEAD_REPLAY :
				RK_PROTECTION_UNAVAILABLE;
		return RK_PROTECTION_AEAD;
	}
	if (needs & RK_NEED_FRESHNESS)
		return has_key && has_replay ?
			RK_PROTECTION_HMAC_SHA256_REPLAY :
			RK_PROTECTION_UNAVAILABLE;
	if (needs & RK_NEED_ORIGIN)
		return has_key ? RK_PROTECTION_HMAC_SHA256 :
			RK_PROTECTION_UNAVAILABLE;
	if (conditions & RK_HAVE_TRUSTED_DIGEST)
		return RK_PROTECTION_SHA256_TRUSTED_DIGEST;
	if (has_key)
		return RK_PROTECTION_HMAC_SHA256;
	return RK_PROTECTION_UNAVAILABLE;
}
