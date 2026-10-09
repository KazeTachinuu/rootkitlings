// SPDX-License-Identifier: GPL-2.0
#include "sha256.h"

int rk_sha256_digest_with(const struct rk_sha256_api *api, void *context,
			  const unsigned char *data, size_t length,
			  unsigned char output[RK_SHA256_DIGEST_SIZE])
{
	(void)api;
	(void)context;
	(void)data;
	(void)length;
	(void)output;
	return -EOPNOTSUPP;
}

int rk_sha256_verify_with(const struct rk_sha256_api *api, void *context,
			  const unsigned char *data, size_t length,
			  const unsigned char expected[RK_SHA256_DIGEST_SIZE],
			  bool *matches)
{
	(void)api;
	(void)context;
	(void)data;
	(void)length;
	(void)expected;
	(void)matches;
	return -EOPNOTSUPP;
}
