/* SPDX-License-Identifier: GPL-2.0 */
#include "aead.h"
#include "aead_platform.h"

static int rk_validate_ops(const struct rk_aead_ops *ops)
{
	if (!ops || !ops->alloc || !ops->is_error || !ops->pointer_error ||
	    !ops->iv_size || !ops->auth_size || !ops->set_key ||
	    !ops->set_auth_size || !ops->request_alloc || !ops->request_free ||
	    !ops->tfm_free || !ops->sg_init_one || !ops->wait_init ||
	    !ops->set_callback || !ops->set_ad || !ops->set_crypt ||
	    !ops->encrypt || !ops->decrypt || !ops->wait_request || !ops->clear)
		return -EINVAL;
	return 0;
}

static int rk_run(const struct rk_aead_ops *ops, const u8 *key,
		  const u8 *nonce, const u8 *aad, size_t aad_length,
		  const u8 *input, size_t input_length, bool decrypt,
		  u8 *output, size_t output_capacity, size_t *output_length)
{
	u8 work[RK_AEAD_WORK_MAX];
	u8 iv[RK_AEAD_NONCE_LENGTH];
	rk_scatterlist sg;
	rk_crypto_wait wait;
	void *tfm = NULL;
	void *request = NULL;
	size_t result_length;
	size_t work_length;
	int error;

	error = rk_validate_ops(ops);
	if (error || !key || !nonce || !aad || !input || !output || !output_length)
		return -EINVAL;
	if (aad_length > RK_AEAD_AAD_MAX)
		return -EMSGSIZE;
	if (decrypt) {
		if (input_length < RK_AEAD_TAG_LENGTH ||
		    input_length > RK_AEAD_SEALED_MAX)
			return -EMSGSIZE;
		result_length = input_length - RK_AEAD_TAG_LENGTH;
	} else {
		if (input_length > RK_AEAD_PLAINTEXT_MAX)
			return -EMSGSIZE;
		result_length = input_length + RK_AEAD_TAG_LENGTH;
	}
	if (output_capacity < result_length)
		return -ENOSPC;
	work_length = aad_length + input_length + (decrypt ? 0 : RK_AEAD_TAG_LENGTH);
	memcpy(work, aad, aad_length);
	memcpy(work + aad_length, input, input_length);
	memset(work + aad_length + input_length, 0,
	       work_length - aad_length - input_length);
	memcpy(iv, nonce, sizeof(iv));

	tfm = ops->alloc();
	if (!tfm || ops->is_error(tfm)) {
		error = tfm ? ops->pointer_error(tfm) : -ENOMEM;
		tfm = NULL;
		goto out;
	}
	if (ops->iv_size(tfm) != RK_AEAD_NONCE_LENGTH) {
		error = -EPROTO;
		goto out;
	}
	error = ops->set_key(tfm, key, RK_AEAD_KEY_LENGTH);
	if (error)
		goto out;
	error = ops->set_auth_size(tfm, RK_AEAD_TAG_LENGTH);
	if (error)
		goto out;
	if (ops->auth_size(tfm) != RK_AEAD_TAG_LENGTH) {
		error = -EPROTO;
		goto out;
	}
	request = ops->request_alloc(tfm);
	if (!request) {
		error = -ENOMEM;
		goto out;
	}
	ops->sg_init_one(&sg, work, (unsigned int)work_length);
	ops->wait_init(&wait);
	ops->set_callback(request, RK_AEAD_MAY_BACKLOG | RK_AEAD_MAY_SLEEP,
			  &wait);
	ops->set_ad(request, (unsigned int)aad_length);
	ops->set_crypt(request, &sg, (unsigned int)input_length, iv);
	error = decrypt ? ops->decrypt(request) : ops->encrypt(request);
	error = ops->wait_request(error, &wait);
	if (error > 0)
		error = -EPROTO;
	if (error)
		goto out;
	memcpy(output, work + aad_length, result_length);
	*output_length = result_length;
out:
	if (request)
		ops->request_free(request);
	if (tfm)
		ops->tfm_free(tfm);
	ops->clear(work, sizeof(work));
	ops->clear(iv, sizeof(iv));
	return error;
}

int rk_aead_seal_with(const struct rk_aead_ops *ops, const u8 *key,
		      const u8 *nonce, const u8 *aad, size_t aad_length,
		      const u8 *plaintext, size_t plaintext_length,
		      u8 *sealed, size_t sealed_capacity, size_t *sealed_length)
{
	return rk_run(ops, key, nonce, aad, aad_length, plaintext,
		      plaintext_length, false, sealed, sealed_capacity,
		      sealed_length);
}

int rk_aead_open_with(const struct rk_aead_ops *ops, const u8 *key,
		      const u8 *nonce, const u8 *aad, size_t aad_length,
		      const u8 *sealed, size_t sealed_length, u8 *plaintext,
		      size_t plaintext_capacity, size_t *plaintext_length)
{
	return rk_run(ops, key, nonce, aad, aad_length, sealed, sealed_length,
		      true, plaintext, plaintext_capacity, plaintext_length);
}

int rk_aead_seal(const u8 *key, const u8 *nonce, const u8 *aad,
		 size_t aad_length, const u8 *plaintext, size_t plaintext_length,
		 u8 *sealed, size_t sealed_capacity, size_t *sealed_length)
{
	return rk_aead_seal_with(&rk_kernel_aead_ops, key, nonce, aad, aad_length,
			 plaintext, plaintext_length, sealed, sealed_capacity,
			 sealed_length);
}

int rk_aead_open(const u8 *key, const u8 *nonce, const u8 *aad,
		 size_t aad_length, const u8 *sealed, size_t sealed_length,
		 u8 *plaintext, size_t plaintext_capacity, size_t *plaintext_length)
{
	return rk_aead_open_with(&rk_kernel_aead_ops, key, nonce, aad, aad_length,
			 sealed, sealed_length, plaintext, plaintext_capacity,
			 plaintext_length);
}
