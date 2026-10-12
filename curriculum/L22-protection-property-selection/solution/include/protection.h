/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_PROTECTION_H
#define RKLINGS_PROTECTION_H

#ifdef __KERNEL__
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <stddef.h>
#include <string.h>
#endif

enum {
	RK_XOR_DATA_MAX = 256,
	RK_XOR_KEY_MAX = 32,
};

enum rk_xor_result {
	RK_XOR_OK,
	RK_XOR_INVALID_ARGUMENT,
	RK_XOR_TOO_LONG,
	RK_XOR_NO_SPACE,
};

enum rk_protection_need {
	RK_NEED_INTEGRITY = 1U << 0,
	RK_NEED_ORIGIN = 1U << 1,
	RK_NEED_FRESHNESS = 1U << 2,
	RK_NEED_CONFIDENTIALITY = 1U << 3,
	RK_NEED_MASK = (1U << 4) - 1,
};

enum rk_protection_condition {
	RK_HAVE_TRUSTED_DIGEST = 1U << 0,
	RK_HAVE_SHARED_KEY = 1U << 1,
	RK_HAVE_REPLAY_STATE = 1U << 2,
	RK_HAVE_UNIQUE_NONCE = 1U << 3,
	RK_HAVE_MASK = (1U << 4) - 1,
};

enum rk_protection_plan {
	RK_PROTECTION_UNAVAILABLE,
	RK_PROTECTION_ENCODING,
	RK_PROTECTION_SHA256_TRUSTED_DIGEST,
	RK_PROTECTION_HMAC_SHA256,
	RK_PROTECTION_HMAC_SHA256_REPLAY,
	RK_PROTECTION_AEAD,
	RK_PROTECTION_AEAD_REPLAY,
};

enum rk_xor_result
rk_xor_repeat(const unsigned char *input, size_t length,
	      const unsigned char *key, size_t key_length,
	      unsigned char *output, size_t capacity);

enum rk_protection_plan
rk_choose_protection(unsigned int needs, unsigned int conditions);

#endif
