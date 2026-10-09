// SPDX-License-Identifier: GPL-2.0
#include "sha256.h"

static bool complete_digest_api(const struct rk_sha256_api *api)
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
	unsigned char temporary[RK_SHA256_DIGEST_SIZE];
	struct shash_desc *request = NULL;
	struct crypto_shash *hash;
	size_t private_size;
	int result = -EINVAL;

	if (!complete_digest_api(api) || !data || !output)
		return result;
	if (length > UINT_MAX)
		return -E2BIG;
	hash = api->crypto_alloc_shash("sha256", 0, 0, context);
	if (!hash)
		return -ENOMEM;
	if (api->is_error(hash, context))
		return api->pointer_error(hash, context);
	if (api->crypto_shash_digestsize(hash, context) !=
	    RK_SHA256_DIGEST_SIZE) {
		result = -EPROTO;
		goto out;
	}
	private_size = api->crypto_shash_descsize(hash, context);
	if (private_size > SIZE_MAX - sizeof(*request)) {
		result = -EOVERFLOW;
		goto out;
	}
	request = api->allocate_descriptor(sizeof(*request) + private_size,
					   context);
	if (!request) {
		result = -ENOMEM;
		goto out;
	}
	request->tfm = hash;
	result = api->crypto_shash_digest(request, data, (unsigned int)length,
					 temporary, context);
	if (result > 0)
		result = -EPROTO;
	if (!result)
		memcpy(output, temporary, sizeof(temporary));
out:
	if (request)
		api->free_descriptor(request, context);
	api->crypto_free_shash(hash, context);
	return result;
}

int rk_sha256_verify_with(const struct rk_sha256_api *api, void *context,
			  const unsigned char *data, size_t length,
			  const unsigned char expected[RK_SHA256_DIGEST_SIZE],
			  bool *matches)
{
	unsigned char actual[RK_SHA256_DIGEST_SIZE];
	int result;

	if (!complete_digest_api(api) || !api->crypto_memneq || !data ||
	    !expected || !matches)
		return -EINVAL;
	result = rk_sha256_digest_with(api, context, data, length, actual);
	if (!result)
		*matches = api->crypto_memneq(actual, expected, sizeof(actual),
					      context) == 0;
	return result;
}
