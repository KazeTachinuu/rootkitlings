/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_AUTH_PLATFORM_H
#define ROOTKITLINGS_AUTH_PLATFORM_H

#include "authenticated.h"

int rk_platform_auth_create(void **context, const u8 *key, size_t key_length);
void rk_platform_auth_destroy(void *context);
int rk_platform_hmac(void *context, const u8 *data, size_t length,
		     u8 digest[RK_AUTH_DIGEST_LENGTH]);
int rk_platform_sign_once(const u8 *key, size_t key_length, const u8 *data,
			  size_t length, u8 digest[RK_AUTH_DIGEST_LENGTH]);
bool rk_platform_tags_equal(const u8 *left, const u8 *right, size_t length);
void rk_platform_lock(void *context);
void rk_platform_unlock(void *context);
void rk_platform_clear(void *buffer, size_t length);

#endif
