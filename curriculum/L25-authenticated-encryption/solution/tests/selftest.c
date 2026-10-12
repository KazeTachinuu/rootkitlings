/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/string.h>

#include "aead.h"
#include "selftest.h"

static bool bytes_are(const u8 *bytes, size_t length, u8 value)
{
	for (size_t i = 0; i < length; i++)
		if (bytes[i] != value)
			return false;
	return true;
}

static int expect_rejected(const u8 *key, const u8 *nonce, const u8 *aad,
			   size_t aad_length, const u8 *sealed,
			   size_t sealed_length)
{
	u8 plaintext[RK_AEAD_PLAINTEXT_MAX];
	size_t plaintext_length = 37;
	int error;

	memset(plaintext, 0xa5, sizeof(plaintext));
	error = rk_aead_open(key, nonce, aad, aad_length, sealed, sealed_length,
			     plaintext, sizeof(plaintext), &plaintext_length);
	if (error != -EBADMSG || plaintext_length != 37 ||
	    !bytes_are(plaintext, sizeof(plaintext), 0xa5))
		return -EBADE;
	return 0;
}

static int nist_vector(void)
{
	static const u8 key[32] = {
		0x92,0xe1,0x1d,0xcd,0xaa,0x86,0x6f,0x5c,
		0xe7,0x90,0xfd,0x24,0x50,0x1f,0x92,0x50,
		0x9a,0xac,0xf4,0xcb,0x8b,0x13,0x39,0xd5,
		0x0c,0x9c,0x12,0x40,0x93,0x5d,0xd0,0x8b,
	};
	static const u8 nonce[12] = {
		0xac,0x93,0xa1,0xa6,0x14,0x52,0x99,0xbd,0xe9,0x02,0xf2,0x1a,
	};
	static const u8 aad[16] = {
		0x1e,0x08,0x89,0x01,0x6f,0x67,0x60,0x1c,
		0x8e,0xbe,0xa4,0x94,0x3b,0xc2,0x3a,0xd6,
	};
	static const u8 plaintext[16] = {
		0x2d,0x71,0xbc,0xfa,0x91,0x4e,0x4a,0xc0,
		0x45,0xb2,0xaa,0x60,0x95,0x5f,0xad,0x24,
	};
	static const u8 expected[32] = {
		0x89,0x95,0xae,0x2e,0x6d,0xf3,0xdb,0xf9,
		0x6f,0xac,0x7b,0x71,0x37,0xba,0xe6,0x7f,
		0xec,0xa5,0xaa,0x77,0xd5,0x1d,0x4a,0x0a,
		0x14,0xd9,0xc5,0x1e,0x1d,0xa4,0x74,0xab,
	};
	u8 sealed[sizeof(expected)], opened[sizeof(plaintext)];
	u8 changed_key[sizeof(key)], changed_nonce[sizeof(nonce)];
	u8 changed_aad[sizeof(aad)], changed_sealed[sizeof(expected)];
	size_t sealed_length = 0, plaintext_length = 0;
	int error;

	error = rk_aead_seal(key, nonce, aad, sizeof(aad), plaintext,
			     sizeof(plaintext), sealed, sizeof(sealed),
			     &sealed_length);
	if (error || sealed_length != sizeof(expected) ||
	    memcmp(sealed, expected, sizeof(expected)))
		return error ?: -EBADE;
	error = rk_aead_open(key, nonce, aad, sizeof(aad), sealed,
			     sealed_length, opened, sizeof(opened),
			     &plaintext_length);
	if (error || plaintext_length != sizeof(plaintext) ||
	    memcmp(opened, plaintext, sizeof(plaintext)))
		return error ?: -EBADE;

	for (size_t i = 0; i < sizeof(sealed); i++) {
		memcpy(changed_sealed, sealed, sizeof(sealed));
		changed_sealed[i] ^= 1;
		error = expect_rejected(key, nonce, aad, sizeof(aad),
					changed_sealed, sizeof(changed_sealed));
		if (error)
			return error;
	}
	for (size_t i = 0; i < sizeof(aad); i++) {
		memcpy(changed_aad, aad, sizeof(aad));
		changed_aad[i] ^= 1;
		error = expect_rejected(key, nonce, changed_aad,
					sizeof(changed_aad), sealed, sizeof(sealed));
		if (error)
			return error;
	}
	for (size_t i = 0; i < sizeof(nonce); i++) {
		memcpy(changed_nonce, nonce, sizeof(nonce));
		changed_nonce[i] ^= 1;
		error = expect_rejected(key, changed_nonce, aad, sizeof(aad),
					sealed, sizeof(sealed));
		if (error)
			return error;
	}
	for (size_t i = 0; i < sizeof(key); i++) {
		memcpy(changed_key, key, sizeof(key));
		changed_key[i] ^= 1;
		error = expect_rejected(changed_key, nonce, aad, sizeof(aad),
					sealed, sizeof(sealed));
		if (error)
			return error;
	}
	return 0;
}

static int boundary_round_trips(void)
{
	static const u8 aad_lengths[] = { 0, 1, 15, 16, 17, 31, 32 };
	static const u8 plaintext_lengths[] = { 0, 1, 15, 16, 17, 31, 32, 33, 63, 64 };
	u8 key[RK_AEAD_KEY_LENGTH], nonce[RK_AEAD_NONCE_LENGTH];
	u8 aad[RK_AEAD_AAD_MAX], plaintext[RK_AEAD_PLAINTEXT_MAX];
	u8 sealed[RK_AEAD_SEALED_MAX + 8], opened[RK_AEAD_PLAINTEXT_MAX + 8];
	unsigned int invocation = 0;
	int error;

	for (size_t i = 0; i < sizeof(key); i++)
		key[i] = (u8)(0x30 + i);
	for (size_t i = 0; i < sizeof(aad); i++)
		aad[i] = (u8)(0x70 + i);
	for (size_t i = 0; i < sizeof(plaintext); i++)
		plaintext[i] = (u8)(0xd0 - i);

	for (size_t a = 0; a < ARRAY_SIZE(aad_lengths); a++) {
		for (size_t p = 0; p < ARRAY_SIZE(plaintext_lengths); p++) {
			size_t sealed_length = 91, plaintext_length = 92;
			size_t aad_length = aad_lengths[a];
			size_t length = plaintext_lengths[p];

			memset(nonce, 0x5c, sizeof(nonce));
			invocation++;
			nonce[8] = (u8)(invocation >> 24);
			nonce[9] = (u8)(invocation >> 16);
			nonce[10] = (u8)(invocation >> 8);
			nonce[11] = (u8)invocation;
			memset(sealed, 0xc7, sizeof(sealed));
			error = rk_aead_seal(key, nonce, aad, aad_length, plaintext,
					     length, sealed,
					     length + RK_AEAD_TAG_LENGTH,
					     &sealed_length);
			if (error || sealed_length != length + RK_AEAD_TAG_LENGTH ||
			    !bytes_are(sealed + sealed_length,
				       sizeof(sealed) - sealed_length, 0xc7))
				return error ?: -EBADE;
			memset(opened, 0xd8, sizeof(opened));
			error = rk_aead_open(key, nonce, aad, aad_length, sealed,
					     sealed_length, opened, length,
					     &plaintext_length);
			if (error || plaintext_length != length ||
			    memcmp(opened, plaintext, length) ||
			    !bytes_are(opened + length,
				       sizeof(opened) - length, 0xd8))
				return error ?: -EBADE;
		}
	}
	return 0;
}

int rk_aead_selftest(void)
{
	int error;

	error = nist_vector();
	if (error)
		return error;
	error = boundary_round_trips();
	if (error)
		return error;
	pr_info("rklings: aes-256-gcm behavior passed\n");
	return 0;
}
