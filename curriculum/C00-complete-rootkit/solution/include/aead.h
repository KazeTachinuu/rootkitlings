/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_AEAD_H
#define RKLINGS_AEAD_H

#include <linux/types.h>

enum {
	RK_AEAD_KEY_LENGTH    = 32,
	RK_AEAD_NONCE_LENGTH  = 12,
	RK_AEAD_TAG_LENGTH    = 16,
	RK_AEAD_AAD_MAX       = 32,
	RK_AEAD_PLAINTEXT_MAX = 2048,
	RK_AEAD_SEALED_MAX    = RK_AEAD_PLAINTEXT_MAX + RK_AEAD_TAG_LENGTH,
};

int rk_aead_seal(const u8 *key, const u8 *nonce,
		 const u8 *aad, size_t aad_len,
		 const u8 *plaintext, size_t plaintext_len,
		 u8 *sealed, size_t sealed_cap, size_t *sealed_len);

int rk_aead_open(const u8 *key, const u8 *nonce,
		 const u8 *aad, size_t aad_len,
		 const u8 *sealed, size_t sealed_len,
		 u8 *plaintext, size_t plaintext_cap, size_t *plaintext_len);

#endif
