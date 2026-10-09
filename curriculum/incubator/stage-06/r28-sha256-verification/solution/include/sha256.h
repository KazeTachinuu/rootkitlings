/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_SHA256_H
#define ROOTKITLINGS_SHA256_H

#ifdef __KERNEL__
#include <crypto/hash.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/limits.h>
#include <linux/stddef.h>
#include <linux/types.h>
#else
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct crypto_shash;
struct shash_desc {
	struct crypto_shash *tfm;
	unsigned char context[];
};
#endif

enum { RK_SHA256_DIGEST_SIZE = 32 };

struct rk_sha256_api {
	struct crypto_shash *(*crypto_alloc_shash)(const char *name,
						    unsigned int type,
						    unsigned int mask,
						    void *context);
	bool (*is_error)(const void *pointer, void *context);
	int (*pointer_error)(const void *pointer, void *context);
	size_t (*crypto_shash_digestsize)(struct crypto_shash *tfm,
						 void *context);
	size_t (*crypto_shash_descsize)(struct crypto_shash *tfm,
					       void *context);
	struct shash_desc *(*allocate_descriptor)(size_t size, void *context);
	void (*free_descriptor)(struct shash_desc *desc, void *context);
	int (*crypto_shash_digest)(struct shash_desc *desc,
				   const unsigned char *data,
				   unsigned int length,
				   unsigned char *output, void *context);
	void (*crypto_free_shash)(struct crypto_shash *tfm, void *context);
	int (*crypto_memneq)(const void *left, const void *right, size_t length,
			     void *context);
};

int rk_sha256_digest_with(const struct rk_sha256_api *api, void *context,
			  const unsigned char *data, size_t length,
			  unsigned char output[RK_SHA256_DIGEST_SIZE]);
int rk_sha256_verify_with(const struct rk_sha256_api *api, void *context,
			  const unsigned char *data, size_t length,
			  const unsigned char expected[RK_SHA256_DIGEST_SIZE],
			  bool *matches);

int rk_sha256_digest(const unsigned char *data, size_t length,
		     unsigned char output[RK_SHA256_DIGEST_SIZE]);
int rk_sha256_verify(const unsigned char *data, size_t length,
		     const unsigned char expected[RK_SHA256_DIGEST_SIZE],
		     bool *matches);

#endif
