// SPDX-License-Identifier: GPL-2.0
#include "sha256.h"

static bool rk_digest_api_valid(const struct rk_sha256_api *api)
{
	return api && api->crypto_alloc_shash && api->is_error &&
	       api->pointer_error && api->crypto_shash_digestsize &&
	       api->crypto_shash_descsize && api->allocate_descriptor &&
	       api->free_descriptor && api->crypto_shash_digest &&
	       api->crypto_free_shash;
}

int rk_sha256_digest_with(const struct rk_sha256_api *api, void *context,
			  const unsigned char *data, size_t length,
			  unsigned char output[RK_SHA256_DIGEST_SIZE])
{
	unsigned char candidate[RK_SHA256_DIGEST_SIZE];
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t allocation_size;
	size_t context_size;
	int error;

	if (!rk_digest_api_valid(api) || !data || !output)
		return -EINVAL;
	if (length > UINT_MAX)
		return -E2BIG;

	tfm = api->crypto_alloc_shash("sha256", 0, 0, context);
	if (!tfm)
		return -ENOMEM;
	if (api->is_error(tfm, context))
		return api->pointer_error(tfm, context);
	if (api->crypto_shash_digestsize(tfm, context) !=
	    RK_SHA256_DIGEST_SIZE) {
		error = -EPROTO;
		goto free_tfm;
	}

	context_size = api->crypto_shash_descsize(tfm, context);
	if (context_size > SIZE_MAX - sizeof(*desc)) {
		error = -EOVERFLOW;
		goto free_tfm;
	}
	allocation_size = sizeof(*desc) + context_size;
	desc = api->allocate_descriptor(allocation_size, context);
	if (!desc) {
		error = -ENOMEM;
		goto free_tfm;
	}
	desc->tfm = tfm;
	error = api->crypto_shash_digest(desc, data, (unsigned int)length,
					 candidate, context);
	api->free_descriptor(desc, context);
	if (error > 0)
		error = -EPROTO;
	if (!error)
		memcpy(output, candidate, sizeof(candidate));
free_tfm:
	api->crypto_free_shash(tfm, context);
	return error;
}

int rk_sha256_verify_with(const struct rk_sha256_api *api, void *context,
			  const unsigned char *data, size_t length,
			  const unsigned char expected[RK_SHA256_DIGEST_SIZE],
			  bool *matches)
{
	unsigned char digest[RK_SHA256_DIGEST_SIZE];
	int error;

	if (!rk_digest_api_valid(api) || !api->crypto_memneq || !data ||
	    !expected || !matches)
		return -EINVAL;
	error = rk_sha256_digest_with(api, context, data, length, digest);
	if (error)
		return error;
	*matches = !api->crypto_memneq(digest, expected, sizeof(digest),
				       context);
	return 0;
}
