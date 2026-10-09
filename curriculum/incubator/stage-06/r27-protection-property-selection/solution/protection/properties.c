// SPDX-License-Identifier: GPL-2.0
#include "protection.h"

enum rk_xor_result
rk_xor_repeat(const unsigned char *input, size_t length,
	      const unsigned char *key, size_t key_length,
	      unsigned char *output, size_t capacity)
{
	unsigned char transformed[RK_XOR_DATA_MAX];
	size_t index;

	if (!input || !key || !output || !key_length)
		return RK_XOR_INVALID_ARGUMENT;
	if (length > RK_XOR_DATA_MAX || key_length > RK_XOR_KEY_MAX)
		return RK_XOR_TOO_LONG;
	if (capacity < length)
		return RK_XOR_NO_SPACE;

	for (index = 0; index < length; index++)
		transformed[index] = input[index] ^ key[index % key_length];
	memcpy(output, transformed, length);
	return RK_XOR_OK;
}

enum rk_protection_plan
rk_choose_protection(unsigned int needs, unsigned int conditions)
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
		if (!(conditions & RK_HAVE_REPLAY_STATE))
			return RK_PROTECTION_UNAVAILABLE;
		return RK_PROTECTION_AEAD_REPLAY;
	}

	if (needs & RK_NEED_FRESHNESS) {
		if (!(conditions & RK_HAVE_SHARED_KEY) ||
		    !(conditions & RK_HAVE_REPLAY_STATE))
			return RK_PROTECTION_UNAVAILABLE;
		return RK_PROTECTION_HMAC_SHA256_REPLAY;
	}

	if (needs & RK_NEED_ORIGIN)
		return conditions & RK_HAVE_SHARED_KEY ?
			RK_PROTECTION_HMAC_SHA256 : RK_PROTECTION_UNAVAILABLE;

	if (needs & RK_NEED_INTEGRITY) {
		if (conditions & RK_HAVE_TRUSTED_DIGEST)
			return RK_PROTECTION_SHA256_TRUSTED_DIGEST;
		if (conditions & RK_HAVE_SHARED_KEY)
			return RK_PROTECTION_HMAC_SHA256;
	}
	return RK_PROTECTION_UNAVAILABLE;
}
