// SPDX-License-Identifier: GPL-2.0
#include <crypto/aead.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "aead.h"

static int rk_run(const u8 *key, const u8 *nonce,
		  const u8 *aad, size_t aad_len,
		  const u8 *input, size_t input_len, bool decrypt,
		  u8 *output, size_t output_cap, size_t *output_len)
{
	u8 iv[RK_AEAD_NONCE_LENGTH];
	struct scatterlist sg;
	struct crypto_wait wait;
	struct crypto_aead *tfm = NULL;
	struct aead_request *req = NULL;
	u8 *work = NULL;
	size_t result_len;
	size_t work_len;
	int error;

	if (!key || !nonce || !aad || !input || !output || !output_len)
		return -EINVAL;
	if (aad_len > RK_AEAD_AAD_MAX)
		return -EMSGSIZE;
	if (decrypt) {
		if (input_len < RK_AEAD_TAG_LENGTH ||
		    input_len > RK_AEAD_SEALED_MAX)
			return -EMSGSIZE;
		result_len = input_len - RK_AEAD_TAG_LENGTH;
	} else {
		if (input_len > RK_AEAD_PLAINTEXT_MAX)
			return -EMSGSIZE;
		result_len = input_len + RK_AEAD_TAG_LENGTH;
	}
	if (output_cap < result_len)
		return -ENOSPC;

	work_len = aad_len + input_len + (decrypt ? 0 : RK_AEAD_TAG_LENGTH);
	work = kmalloc(work_len, GFP_KERNEL);
	if (!work)
		return -ENOMEM;
	memcpy(work, aad, aad_len);
	memcpy(work + aad_len, input, input_len);
	memset(work + aad_len + input_len, 0,
	       work_len - aad_len - input_len);
	memcpy(iv, nonce, sizeof(iv));

	tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		error = PTR_ERR(tfm);
		tfm = NULL;
		goto out;
	}
	if (crypto_aead_ivsize(tfm) != RK_AEAD_NONCE_LENGTH) {
		error = -EPROTO;
		goto out;
	}
	error = crypto_aead_setkey(tfm, key, RK_AEAD_KEY_LENGTH);
	if (error)
		goto out;
	error = crypto_aead_setauthsize(tfm, RK_AEAD_TAG_LENGTH);
	if (error)
		goto out;

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		error = -ENOMEM;
		goto out;
	}
	sg_init_one(&sg, work, (unsigned int)work_len);
	crypto_init_wait(&wait);
	aead_request_set_callback(req,
				  CRYPTO_TFM_REQ_MAY_BACKLOG |
				  CRYPTO_TFM_REQ_MAY_SLEEP,
				  crypto_req_done, &wait);
	aead_request_set_ad(req, (unsigned int)aad_len);
	aead_request_set_crypt(req, &sg, &sg, (unsigned int)input_len, iv);

	error = crypto_wait_req(decrypt ? crypto_aead_decrypt(req)
				        : crypto_aead_encrypt(req),
			        &wait);
	if (error)
		goto out;
	memcpy(output, work + aad_len, result_len);
	*output_len = result_len;
out:
	if (req)
		aead_request_free(req);
	if (tfm)
		crypto_free_aead(tfm);
	if (work) {
		memzero_explicit(work, work_len);
		kfree(work);
	}
	memzero_explicit(iv, sizeof(iv));
	return error;
}

int rk_aead_seal(const u8 *key, const u8 *nonce,
		 const u8 *aad, size_t aad_len,
		 const u8 *plaintext, size_t plaintext_len,
		 u8 *sealed, size_t sealed_cap, size_t *sealed_len)
{
	return rk_run(key, nonce, aad, aad_len, plaintext, plaintext_len,
		      false, sealed, sealed_cap, sealed_len);
}

int rk_aead_open(const u8 *key, const u8 *nonce,
		 const u8 *aad, size_t aad_len,
		 const u8 *sealed, size_t sealed_len,
		 u8 *plaintext, size_t plaintext_cap, size_t *plaintext_len)
{
	return rk_run(key, nonce, aad, aad_len, sealed, sealed_len,
		      true, plaintext, plaintext_cap, plaintext_len);
}
