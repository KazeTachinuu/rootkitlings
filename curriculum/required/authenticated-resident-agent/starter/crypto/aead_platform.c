/* SPDX-License-Identifier: GPL-2.0 */
#include <crypto/aead.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "aead.h"
#include "aead_platform.h"

#if ROOTKITLINGS_FAIL_POINT < 0 || ROOTKITLINGS_FAIL_POINT > 14
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static void *rk_alloc(void) { if (ROOTKITLINGS_FAIL_POINT == 1) return ERR_PTR(-ENOMEM); return crypto_alloc_aead("gcm(aes)", 0, 0); }
static bool rk_is_error(const void *p) { return IS_ERR(p); }
static int rk_pointer_error(const void *p) { return PTR_ERR(p); }
static unsigned int rk_iv_size(void *p) { return crypto_aead_ivsize(p); }
static unsigned int rk_auth_size(void *p) { return crypto_aead_authsize(p); }
static int rk_set_key(void *p, const u8 *k, unsigned int n) { if (ROOTKITLINGS_FAIL_POINT == 2) return -EKEYREJECTED; return crypto_aead_setkey(p, k, n); }
static int rk_set_auth_size(void *p, unsigned int n) { return crypto_aead_setauthsize(p, n); }
static void *rk_request_alloc(void *p) { return aead_request_alloc(p, GFP_KERNEL); }
static void rk_request_free(void *p) { aead_request_free(p); }
static void rk_tfm_free(void *p) { crypto_free_aead(p); }
static void rk_sg_init(void *s, void *b, unsigned int n) { sg_init_one(s, b, n); }
static void rk_wait_init(void *w) { crypto_init_wait(w); }
static void rk_set_callback(void *r, unsigned int flags, void *w)
{
	unsigned int kernel_flags = 0;
	if (flags & RK_AEAD_MAY_BACKLOG) kernel_flags |= CRYPTO_TFM_REQ_MAY_BACKLOG;
	if (flags & RK_AEAD_MAY_SLEEP) kernel_flags |= CRYPTO_TFM_REQ_MAY_SLEEP;
	aead_request_set_callback(r, kernel_flags, crypto_req_done, w);
}
static void rk_set_ad(void *r, unsigned int n) { aead_request_set_ad(r, n); }
static void rk_set_crypt(void *r, void *s, unsigned int n, u8 *iv) { aead_request_set_crypt(r, s, s, n, iv); }
static int rk_encrypt(void *r) { if (ROOTKITLINGS_FAIL_POINT == 3) return -EIO; return crypto_aead_encrypt(r); }
static int rk_decrypt(void *r) { return crypto_aead_decrypt(r); }
static int rk_wait_request(int e, void *w) { return crypto_wait_req(e, w); }
static void rk_clear(void *p, size_t n) { memzero_explicit(p, n); }
const struct rk_aead_ops rk_kernel_aead_ops = {
	.alloc=rk_alloc,.is_error=rk_is_error,.pointer_error=rk_pointer_error,
	.iv_size=rk_iv_size,.auth_size=rk_auth_size,.set_key=rk_set_key,
	.set_auth_size=rk_set_auth_size,.request_alloc=rk_request_alloc,
	.request_free=rk_request_free,.tfm_free=rk_tfm_free,.sg_init_one=rk_sg_init,
	.wait_init=rk_wait_init,.set_callback=rk_set_callback,.set_ad=rk_set_ad,
	.set_crypt=rk_set_crypt,.encrypt=rk_encrypt,.decrypt=rk_decrypt,
	.wait_request=rk_wait_request,.clear=rk_clear,
};
