// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <crypto/utils.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "sha256.h"

#ifndef RKLINGS_FAIL_POINT
#define RKLINGS_FAIL_POINT 0
#endif

static struct crypto_shash *rk_alloc(const char *name, unsigned int type,
				     unsigned int mask, void *context)
{
	(void)context;
	if (RKLINGS_FAIL_POINT == 1)
		return ERR_PTR(-EINTR);
	return crypto_alloc_shash(name, type, mask);
}

static bool rk_is_error(const void *pointer, void *context)
{
	(void)context;
	return IS_ERR(pointer);
}

static int rk_pointer_error(const void *pointer, void *context)
{
	(void)context;
	return PTR_ERR(pointer);
}

static size_t rk_digest_size(struct crypto_shash *tfm, void *context)
{
	(void)context;
	return crypto_shash_digestsize(tfm);
}

static size_t rk_descriptor_size(struct crypto_shash *tfm, void *context)
{
	(void)context;
	return crypto_shash_descsize(tfm);
}

static struct shash_desc *rk_allocate(size_t size, void *context)
{
	(void)context;
	return kmalloc(size, GFP_KERNEL);
}

static void rk_deallocate(struct shash_desc *desc, void *context)
{
	(void)context;
	kfree(desc);
}

static int rk_digest(struct shash_desc *desc, const unsigned char *data,
		     unsigned int length, unsigned char *output, void *context)
{
	(void)context;
	return crypto_shash_digest(desc, data, length, output);
}

static void rk_free(struct crypto_shash *tfm, void *context)
{
	(void)context;
	crypto_free_shash(tfm);
}

static int rk_different(const void *left, const void *right, size_t length,
			void *context)
{
	(void)context;
	return crypto_memneq(left, right, length);
}

static const struct rk_sha256_api rk_kernel_api = {
	.crypto_alloc_shash = rk_alloc,
	.is_error = rk_is_error,
	.pointer_error = rk_pointer_error,
	.crypto_shash_digestsize = rk_digest_size,
	.crypto_shash_descsize = rk_descriptor_size,
	.allocate_descriptor = rk_allocate,
	.free_descriptor = rk_deallocate,
	.crypto_shash_digest = rk_digest,
	.crypto_free_shash = rk_free,
	.crypto_memneq = rk_different,
};

int rk_sha256_digest(const unsigned char *data, size_t length,
		     unsigned char output[RK_SHA256_DIGEST_SIZE])
{
	return rk_sha256_digest_with(&rk_kernel_api, NULL, data, length, output);
}

int rk_sha256_verify(const unsigned char *data, size_t length,
		     const unsigned char expected[RK_SHA256_DIGEST_SIZE],
		     bool *matches)
{
	return rk_sha256_verify_with(&rk_kernel_api, NULL, data, length, expected,
				     matches);
}
