// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/string.h>

#include "protection.h"
#include "selftest.h"

static int rk_expect(bool condition, unsigned int *assertions)
{
	(*assertions)++;
	return condition ? 0 : -EINVAL;
}

#define RK_EXPECT(condition) do { \
	if (rk_expect((condition), assertions)) \
		return -EINVAL; \
} while (0)

static enum rk_protection_plan
rk_expected_plan(unsigned int needs, unsigned int conditions)
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

int rk_protection_selftest(unsigned int *assertions, char *runtime_marker,
			   size_t marker_capacity)
{
	static const unsigned char encoded_marker[] = {
		0x19, 0x0e, 0x54, 0x19, 0x10, 0x17, 0x1f,
		0x0c, 0x14, 0x0e, 0x48, 0x09, 0x07, 0x04,
		0x10, 0x05, 0x11, 0x1c, 0x13, 0x11, 0x54,
		0x1d, 0x0c, 0x0a, 0x02, 0x07, 0x15, 0x0e,
	};
	static const unsigned char key[] = { 0x6b, 0x65, 0x79 };
	unsigned char input[RK_XOR_DATA_MAX];
	unsigned char original[RK_XOR_DATA_MAX];
	unsigned int conditions, needs;
	size_t index;

	if (!assertions || !runtime_marker ||
	    marker_capacity <= sizeof(encoded_marker))
		return -EINVAL;
	*assertions = 0;

	for (index = 0; index < sizeof(input); index++)
		input[index] = (unsigned char)index;
	memcpy(original, input, sizeof(input));
	RK_EXPECT(rk_xor_repeat(input, sizeof(input), key, sizeof(key),
				input, sizeof(input)) == RK_XOR_OK);
	RK_EXPECT(memcmp(input, original, sizeof(input)) != 0);
	RK_EXPECT(rk_xor_repeat(input, sizeof(input), key, sizeof(key),
				input, sizeof(input)) == RK_XOR_OK);
	RK_EXPECT(!memcmp(input, original, sizeof(input)));

	for (needs = 0; needs <= RK_NEED_MASK + 1; needs++) {
		for (conditions = 0; conditions <= RK_HAVE_MASK + 1;
		     conditions++)
			RK_EXPECT(rk_choose_protection(needs, conditions) ==
				  rk_expected_plan(needs, conditions));
	}

	RK_EXPECT(rk_xor_repeat(encoded_marker, sizeof(encoded_marker),
				key, sizeof(key),
				(unsigned char *)runtime_marker,
				marker_capacity - 1) == RK_XOR_OK);
	runtime_marker[sizeof(encoded_marker)] = '\0';
	return 0;
}
