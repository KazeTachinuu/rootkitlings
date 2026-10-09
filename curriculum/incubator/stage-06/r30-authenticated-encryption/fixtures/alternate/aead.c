/* SPDX-License-Identifier: GPL-2.0 */
#include "aead.h"
#include "platform.h"

static bool complete_ops(const struct rk_aead_ops *o)
{
	return o && o->alloc && o->is_error && o->pointer_error && o->iv_size &&
		o->auth_size && o->set_key && o->set_auth_size && o->request_alloc &&
		o->request_free && o->tfm_free && o->sg_init_one && o->wait_init &&
		o->set_callback && o->set_ad && o->set_crypt && o->encrypt &&
		o->decrypt && o->wait_request && o->clear;
}

static int transform(const struct rk_aead_ops *o, bool opening,
		     const u8 *key, const u8 *nonce, const u8 *aad, size_t alen,
		     const u8 *src, size_t srclen, u8 *dst, size_t capacity,
		     size_t *written)
{
	u8 storage[RK_AEAD_WORK_MAX], iv[RK_AEAD_NONCE_LENGTH];
	rk_scatterlist list;
	rk_crypto_wait completion;
	void *tfm = NULL, *request = NULL;
	size_t produced, allocation;
	int result = -EINVAL;

	if (!complete_ops(o) || !key || !nonce || !aad || !src || !dst || !written)
		return -EINVAL;
	if (alen > RK_AEAD_AAD_MAX)
		return -EMSGSIZE;
	if (opening) {
		if (srclen < RK_AEAD_TAG_LENGTH || srclen > RK_AEAD_SEALED_MAX)
			return -EMSGSIZE;
		produced = srclen - RK_AEAD_TAG_LENGTH;
		allocation = alen + srclen;
	} else {
		if (srclen > RK_AEAD_PLAINTEXT_MAX)
			return -EMSGSIZE;
		produced = srclen + RK_AEAD_TAG_LENGTH;
		allocation = alen + produced;
	}
	if (capacity < produced)
		return -ENOSPC;
	for (size_t n = 0; n < alen; ++n) storage[n] = aad[n];
	for (size_t n = 0; n < srclen; ++n) storage[alen + n] = src[n];
	for (size_t n = alen + srclen; n < allocation; ++n) storage[n] = 0;
	for (size_t n = 0; n < sizeof(iv); ++n) iv[n] = nonce[n];

	tfm = o->alloc();
	if (!tfm) { result = -ENOMEM; goto cleanup; }
	if (o->is_error(tfm)) { result = o->pointer_error(tfm); tfm = NULL; goto cleanup; }
	if (o->iv_size(tfm) != sizeof(iv)) { result = -EPROTO; goto cleanup; }
	result = o->set_key(tfm, key, RK_AEAD_KEY_LENGTH);
	if (result) goto cleanup;
	result = o->set_auth_size(tfm, RK_AEAD_TAG_LENGTH);
	if (result) goto cleanup;
	if (o->auth_size(tfm) != RK_AEAD_TAG_LENGTH) { result = -EPROTO; goto cleanup; }
	request = o->request_alloc(tfm);
	if (!request) { result = -ENOMEM; goto cleanup; }
	o->sg_init_one(&list, storage, (unsigned int)allocation);
	o->wait_init(&completion);
	o->set_callback(request, RK_AEAD_MAY_SLEEP | RK_AEAD_MAY_BACKLOG, &completion);
	o->set_ad(request, (unsigned int)alen);
	o->set_crypt(request, &list, (unsigned int)srclen, iv);
	result = opening ? o->decrypt(request) : o->encrypt(request);
	result = o->wait_request(result, &completion);
	if (result > 0) result = -EPROTO;
	if (!result) {
		for (size_t n = 0; n < produced; ++n) dst[n] = storage[alen + n];
		*written = produced;
	}
cleanup:
	if (request) o->request_free(request);
	if (tfm) o->tfm_free(tfm);
	o->clear(storage, sizeof(storage));
	o->clear(iv, sizeof(iv));
	return result;
}

int rk_aead_seal_with(const struct rk_aead_ops *o,const u8 *k,const u8 *n,const u8 *a,size_t al,const u8 *p,size_t pl,u8 *s,size_t sc,size_t *sl)
{ return transform(o,false,k,n,a,al,p,pl,s,sc,sl); }
int rk_aead_open_with(const struct rk_aead_ops *o,const u8 *k,const u8 *n,const u8 *a,size_t al,const u8 *s,size_t sl,u8 *p,size_t pc,size_t *pl)
{ return transform(o,true,k,n,a,al,s,sl,p,pc,pl); }
int rk_aead_seal(const u8 *k,const u8 *n,const u8 *a,size_t al,const u8 *p,size_t pl,u8 *s,size_t sc,size_t *sl)
{ return rk_aead_seal_with(&rk_kernel_aead_ops,k,n,a,al,p,pl,s,sc,sl); }
int rk_aead_open(const u8 *k,const u8 *n,const u8 *a,size_t al,const u8 *s,size_t sl,u8 *p,size_t pc,size_t *pl)
{ return rk_aead_open_with(&rk_kernel_aead_ops,k,n,a,al,s,sl,p,pc,pl); }
