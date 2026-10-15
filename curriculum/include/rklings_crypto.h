/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rklings_crypto.h - Black-box cryptographic primitives for rootkitlings
 *
 * Provides SHA-256, HMAC-SHA256, and AES-256-GCM via the kernel crypto API.
 * Students use these as opaque calls in L19-L20, then open and rewrite
 * them from scratch in L21.
 */
#ifndef _RKLINGS_CRYPTO_H
#define _RKLINGS_CRYPTO_H

#include <crypto/aead.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#define RK_SHA256_SIZE    32
#define RK_HMAC_KEY_SIZE  32
#define RK_AES_KEY_SIZE   32
#define RK_AES_IV_SIZE    12
#define RK_AES_TAG_SIZE   16

/* SHA-256 of a buffer. Returns 0 on success. */
static inline int rk_sha256(const void *data, size_t len,
			     u8 out[RK_SHA256_SIZE])
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	int ret;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	desc->tfm = tfm;
	ret = crypto_shash_digest(desc, data, len, out);

	kfree(desc);
	crypto_free_shash(tfm);
	return ret;
}

/* HMAC-SHA256 of a buffer with a 32-byte key. Returns 0 on success. */
static inline int rk_hmac_sha256(const u8 key[RK_HMAC_KEY_SIZE],
				 const void *data, size_t len,
				 u8 out[RK_SHA256_SIZE])
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	int ret;

	tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	ret = crypto_shash_setkey(tfm, key, RK_HMAC_KEY_SIZE);
	if (ret) {
		crypto_free_shash(tfm);
		return ret;
	}

	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	desc->tfm = tfm;
	ret = crypto_shash_digest(desc, data, len, out);

	kfree(desc);
	crypto_free_shash(tfm);
	return ret;
}

/* HMAC-SHA256 verify: compute and compare. Returns 0 if valid. */
static inline int rk_hmac_sha256_verify(const u8 key[RK_HMAC_KEY_SIZE],
					const void *data, size_t len,
					const u8 expected[RK_SHA256_SIZE])
{
	u8 computed[RK_SHA256_SIZE];
	int ret;

	ret = rk_hmac_sha256(key, data, len, computed);
	if (ret)
		return ret;

	if (crypto_memneq(computed, expected, RK_SHA256_SIZE))
		return -EBADMSG;

	return 0;
}

/*
 * AES-256-GCM encrypt in place.
 * Input:  plaintext in buf, plaintext_len bytes.
 * Output: buf holds ciphertext (same length), tag appended (16 bytes).
 * buf must have room for plaintext_len + RK_AES_TAG_SIZE.
 * iv is filled with 12 random bytes on success.
 * Returns 0 on success.
 */
static inline int rk_aes_gcm_encrypt(const u8 key[RK_AES_KEY_SIZE],
				      u8 iv[RK_AES_IV_SIZE],
				      void *buf, size_t plaintext_len)
{
	struct crypto_aead *tfm;
	struct aead_request *req;
	struct scatterlist sg;
	int ret;

	tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	ret = crypto_aead_setkey(tfm, key, RK_AES_KEY_SIZE);
	if (ret)
		goto free_tfm;

	ret = crypto_aead_setauthsize(tfm, RK_AES_TAG_SIZE);
	if (ret)
		goto free_tfm;

	get_random_bytes(iv, RK_AES_IV_SIZE);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto free_tfm;
	}

	sg_init_one(&sg, buf, plaintext_len + RK_AES_TAG_SIZE);
	aead_request_set_crypt(req, &sg, &sg, plaintext_len, iv);
	aead_request_set_ad(req, 0);

	ret = crypto_aead_encrypt(req);

	aead_request_free(req);
free_tfm:
	crypto_free_aead(tfm);
	return ret;
}

/*
 * AES-256-GCM decrypt in place.
 * Input:  buf holds ciphertext + tag, total ciphertext_len bytes
 *         (ciphertext_len includes the 16-byte tag).
 * Output: buf holds plaintext (ciphertext_len - RK_AES_TAG_SIZE bytes).
 * Returns 0 on success, -EBADMSG on authentication failure.
 */
static inline int rk_aes_gcm_decrypt(const u8 key[RK_AES_KEY_SIZE],
				      const u8 iv[RK_AES_IV_SIZE],
				      void *buf, size_t ciphertext_len)
{
	struct crypto_aead *tfm;
	struct aead_request *req;
	struct scatterlist sg;
	int ret;

	if (ciphertext_len < RK_AES_TAG_SIZE)
		return -EINVAL;

	tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	ret = crypto_aead_setkey(tfm, key, RK_AES_KEY_SIZE);
	if (ret)
		goto free_tfm;

	ret = crypto_aead_setauthsize(tfm, RK_AES_TAG_SIZE);
	if (ret)
		goto free_tfm;

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto free_tfm;
	}

	sg_init_one(&sg, buf, ciphertext_len);
	aead_request_set_crypt(req, &sg, &sg, ciphertext_len, (u8 *)iv);
	aead_request_set_ad(req, 0);

	ret = crypto_aead_decrypt(req);

	aead_request_free(req);
free_tfm:
	crypto_free_aead(tfm);
	return ret;
}

#endif /* _RKLINGS_CRYPTO_H */
