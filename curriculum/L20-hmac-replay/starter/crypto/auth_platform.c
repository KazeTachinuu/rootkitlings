// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <crypto/utils.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "auth_platform.h"

#ifndef RKLINGS_FAIL_POINT
#define RKLINGS_FAIL_POINT 0
#endif
#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 3
#error "RKLINGS_FAIL_POINT must be between 0 and 3"
#endif

struct rk_auth_context {
	struct crypto_shash *transform;
	struct mutex lock;
};

int rk_platform_auth_create(void **context, const u8 *key, size_t key_length)
{
	struct rk_auth_context *created;
	int error;

	if (!context || !key || key_length > UINT_MAX)
		return -EINVAL;
	if (RKLINGS_FAIL_POINT == 1)
		return -ENOMEM;
	created = kzalloc(sizeof(*created), GFP_KERNEL);
	if (!created)
		return -ENOMEM;
	created->transform = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(created->transform)) {
		error = PTR_ERR(created->transform);
		goto out_free;
	}
	if (crypto_shash_digestsize(created->transform) !=
	    RK_AUTH_DIGEST_LENGTH) {
		error = -EINVAL;
		goto out_transform;
	}
	if (RKLINGS_FAIL_POINT == 2)
		error = -EKEYREJECTED;
	else
		error = crypto_shash_setkey(created->transform, key,
					    (unsigned int)key_length);
	if (error)
		goto out_transform;
	mutex_init(&created->lock);
	*context = created;
	return 0;

out_transform:
	crypto_free_shash(created->transform);
out_free:
	memzero_explicit(created, sizeof(*created));
	kfree(created);
	return error;
}

void rk_platform_auth_destroy(void *context)
{
	struct rk_auth_context *owned = context;

	if (!owned)
		return;
	crypto_free_shash(owned->transform);
	memzero_explicit(owned, sizeof(*owned));
	kfree(owned);
}

int rk_platform_hmac(void *context, const u8 *data, size_t length,
		     u8 digest[RK_AUTH_DIGEST_LENGTH])
{
	struct rk_auth_context *owned = context;
	SHASH_DESC_ON_STACK(descriptor, owned->transform);
	int error;

	if (RKLINGS_FAIL_POINT == 3)
		return -EIO;
	descriptor->tfm = owned->transform;
	error = crypto_shash_digest(descriptor, data, length, digest);
	shash_desc_zero(descriptor);
	return error;
}

int rk_platform_sign_once(const u8 *key, size_t key_length, const u8 *data,
			  size_t length, u8 digest[RK_AUTH_DIGEST_LENGTH])
{
	void *context = NULL;
	int error;

	error = rk_platform_auth_create(&context, key, key_length);
	if (error)
		return error;
	error = rk_platform_hmac(context, data, length, digest);
	rk_platform_auth_destroy(context);
	return error;
}

bool rk_platform_tags_equal(const u8 *left, const u8 *right, size_t length)
{
	return crypto_memneq(left, right, length) == 0;
}

void rk_platform_lock(void *context)
{
	struct rk_auth_context *owned = context;

	mutex_lock(&owned->lock);
}

void rk_platform_unlock(void *context)
{
	struct rk_auth_context *owned = context;

	mutex_unlock(&owned->lock);
}

void rk_platform_clear(void *buffer, size_t length)
{
	memzero_explicit(buffer, length);
}
