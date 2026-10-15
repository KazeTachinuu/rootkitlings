// SPDX-License-Identifier: GPL-2.0
#include <crypto/aead.h>
#include <crypto/gcm.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#define RK_KEY_LEN	32
#define RK_NONCE_LEN	12
#define RK_TAG_LEN	16

/*
 * Encrypt and authenticate plaintext with AES-256-GCM.
 * out must have room for pt_len + RK_TAG_LEN bytes.
 * Returns 0 on success, negative error otherwise.
 */
static int rk_aead_seal(struct crypto_aead *tfm,
			const u8 *nonce,
			const u8 *aad, size_t aad_len,
			const u8 *plaintext, size_t pt_len,
			u8 *out, size_t *out_len)
{
	struct aead_request *req;
	struct scatterlist sg;
	u8 *buf;
	int error;

	buf = kmalloc(aad_len + pt_len + RK_TAG_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* AAD followed by plaintext; tag appended in place by encrypt */
	if (aad_len)
		memcpy(buf, aad, aad_len);
	memcpy(buf + aad_len, plaintext, pt_len);

	sg_init_one(&sg, buf, aad_len + pt_len + RK_TAG_LEN);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		kfree(buf);
		return -ENOMEM;
	}

	aead_request_set_crypt(req, &sg, &sg, pt_len, (u8 *)nonce);
	aead_request_set_ad(req, aad_len);

	error = crypto_aead_encrypt(req);
	if (!error) {
		/* Output is ciphertext + tag, skip AAD prefix */
		memcpy(out, buf + aad_len, pt_len + RK_TAG_LEN);
		*out_len = pt_len + RK_TAG_LEN;
	}

	aead_request_free(req);
	kfree(buf);
	return error;
}

/*
 * Decrypt and verify ciphertext with AES-256-GCM.
 * ct_len includes the 16-byte authentication tag.
 * Returns 0 on success, -EBADMSG on authentication failure.
 */
static int rk_aead_open(struct crypto_aead *tfm,
			const u8 *nonce,
			const u8 *aad, size_t aad_len,
			const u8 *ciphertext, size_t ct_len,
			u8 *out, size_t *out_len)
{
	struct aead_request *req;
	struct scatterlist sg;
	u8 *buf;
	size_t pt_len;
	int error;

	if (ct_len < RK_TAG_LEN)
		return -EINVAL;

	pt_len = ct_len - RK_TAG_LEN;

	buf = kmalloc(aad_len + ct_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* AAD followed by ciphertext + tag */
	if (aad_len)
		memcpy(buf, aad, aad_len);
	memcpy(buf + aad_len, ciphertext, ct_len);

	sg_init_one(&sg, buf, aad_len + ct_len);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		kfree(buf);
		return -ENOMEM;
	}

	aead_request_set_crypt(req, &sg, &sg, ct_len, (u8 *)nonce);
	aead_request_set_ad(req, aad_len);

	error = crypto_aead_decrypt(req);
	if (!error) {
		memcpy(out, buf + aad_len, pt_len);
		*out_len = pt_len;
	}

	aead_request_free(req);
	kfree(buf);
	return error;
}

static int __init rk_init(void)
{
	struct crypto_aead *tfm;
	u8 key[RK_KEY_LEN];
	u8 nonce[RK_NONCE_LEN];
	u8 sealed[64];
	u8 opened[64];
	size_t sealed_len, opened_len;
	int error;

	tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	error = crypto_aead_setauthsize(tfm, RK_TAG_LEN);
	if (error)
		goto out;

	get_random_bytes(key, sizeof(key));
	error = crypto_aead_setkey(tfm, key, RK_KEY_LEN);
	if (error)
		goto out;

	get_random_bytes(nonce, sizeof(nonce));

	/* Seal "hello" */
	error = rk_aead_seal(tfm, nonce, NULL, 0,
			     "hello", 5, sealed, &sealed_len);
	if (error)
		goto out;

	/* Open the sealed ciphertext */
	error = rk_aead_open(tfm, nonce, NULL, 0,
			     sealed, sealed_len, opened, &opened_len);
	if (error)
		goto out;

	/* Verify round-trip */
	if (opened_len != 5 || memcmp(opened, "hello", 5) != 0) {
		error = -EIO;
		goto out;
	}

	/* Tamper: flip a byte in ciphertext, expect -EBADMSG */
	sealed[0] ^= 0x01;
	error = rk_aead_open(tfm, nonce, NULL, 0,
			     sealed, sealed_len, opened, &opened_len);
	if (error == -EBADMSG)
		error = 0;
	else
		error = -EIO;

out:
	memzero_explicit(key, sizeof(key));
	crypto_free_aead(tfm);
	return error;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AES-256-GCM encryption with kernel AEAD API");
