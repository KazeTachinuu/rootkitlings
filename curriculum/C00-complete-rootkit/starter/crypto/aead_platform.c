/* SPDX-License-Identifier: GPL-2.0 */
#include <crypto/aead.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "aead.h"
#include "aead_platform.h"

static void *rk_alloc(void)
{
	if (ROOTKITLINGS_FAIL_POINT == 1)
		return ERR_PTR(-ENOMEM);
	return crypto_alloc_aead("gcm(aes)", 0, 0);
}

static bool rk_is_error(const void *pointer)
{
	return IS_ERR(pointer);
}

static int rk_pointer_error(const void *pointer)
{
	return PTR_ERR(pointer);
}

static unsigned int rk_iv_size(void *transform)
{
	return crypto_aead_ivsize(transform);
}

static unsigned int rk_auth_size(void *transform)
{
	return crypto_aead_authsize(transform);
}

static int rk_set_key(void *transform, const u8 *key, unsigned int length)
{
	if (ROOTKITLINGS_FAIL_POINT == 2)
		return -EKEYREJECTED;
	return crypto_aead_setkey(transform, key, length);
}

static int rk_set_auth_size(void *transform, unsigned int length)
{
	return crypto_aead_setauthsize(transform, length);
}

static void *rk_request_alloc(void *transform)
{
	return aead_request_alloc(transform, GFP_KERNEL);
}

static void rk_request_free(void *request)
{
	aead_request_free(request);
}

static void rk_tfm_free(void *transform)
{
	crypto_free_aead(transform);
}

static void rk_sg_init(void *scatterlist, void *buffer, unsigned int length)
{
	sg_init_one(scatterlist, buffer, length);
}

static void rk_wait_init(void *wait)
{
	crypto_init_wait(wait);
}
static void rk_set_callback(void *r, unsigned int flags, void *w)
{
	unsigned int kernel_flags = 0;
	if (flags & RK_AEAD_MAY_BACKLOG) kernel_flags |= CRYPTO_TFM_REQ_MAY_BACKLOG;
	if (flags & RK_AEAD_MAY_SLEEP) kernel_flags |= CRYPTO_TFM_REQ_MAY_SLEEP;
	aead_request_set_callback(r, kernel_flags, crypto_req_done, w);
}
static void rk_set_ad(void *request, unsigned int length)
{
	aead_request_set_ad(request, length);
}

static void rk_set_crypt(void *request, void *scatterlist,
			 unsigned int length, u8 *iv)
{
	aead_request_set_crypt(request, scatterlist, scatterlist, length, iv);
}

static int rk_encrypt(void *request)
{
	if (ROOTKITLINGS_FAIL_POINT == 3)
		return -EIO;
	return crypto_aead_encrypt(request);
}

static int rk_decrypt(void *request)
{
	return crypto_aead_decrypt(request);
}

static int rk_wait_request(int error, void *wait)
{
	return crypto_wait_req(error, wait);
}

static void rk_clear(void *buffer, size_t length)
{
	memzero_explicit(buffer, length);
}

const struct rk_aead_ops rk_kernel_aead_ops = {
	.alloc = rk_alloc,
	.is_error = rk_is_error,
	.pointer_error = rk_pointer_error,
	.iv_size = rk_iv_size,
	.auth_size = rk_auth_size,
	.set_key = rk_set_key,
	.set_auth_size = rk_set_auth_size,
	.request_alloc = rk_request_alloc,
	.request_free = rk_request_free,
	.tfm_free = rk_tfm_free,
	.sg_init_one = rk_sg_init,
	.wait_init = rk_wait_init,
	.set_callback = rk_set_callback,
	.set_ad = rk_set_ad,
	.set_crypt = rk_set_crypt,
	.encrypt = rk_encrypt,
	.decrypt = rk_decrypt,
	.wait_request = rk_wait_request,
	.clear = rk_clear,
};
