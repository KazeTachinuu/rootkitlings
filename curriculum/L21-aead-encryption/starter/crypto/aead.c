/* SPDX-License-Identifier: GPL-2.0 */
#include "aead.h"
#include "aead_platform.h"

int rk_aead_seal_with(const struct rk_aead_ops *ops, const u8 *key,
		      const u8 *nonce, const u8 *aad, size_t aad_length,
		      const u8 *plaintext, size_t plaintext_length,
		      u8 *sealed, size_t sealed_capacity, size_t *sealed_length)
{
	(void)ops; (void)key; (void)nonce; (void)aad; (void)aad_length;
	(void)plaintext; (void)plaintext_length; (void)sealed;
	(void)sealed_capacity; (void)sealed_length;
	return -EOPNOTSUPP;
}
int rk_aead_open_with(const struct rk_aead_ops *ops, const u8 *key,
		      const u8 *nonce, const u8 *aad, size_t aad_length,
		      const u8 *sealed, size_t sealed_length, u8 *plaintext,
		      size_t plaintext_capacity, size_t *plaintext_length)
{
	(void)ops; (void)key; (void)nonce; (void)aad; (void)aad_length;
	(void)sealed; (void)sealed_length; (void)plaintext;
	(void)plaintext_capacity; (void)plaintext_length;
	return -EOPNOTSUPP;
}
int rk_aead_seal(const u8 *k,const u8 *n,const u8 *a,size_t al,const u8 *p,size_t pl,u8 *s,size_t sc,size_t *sl)
{ return rk_aead_seal_with(&rk_kernel_aead_ops,k,n,a,al,p,pl,s,sc,sl); }
int rk_aead_open(const u8 *k,const u8 *n,const u8 *a,size_t al,const u8 *s,size_t sl,u8 *p,size_t pc,size_t *pl)
{ return rk_aead_open_with(&rk_kernel_aead_ops,k,n,a,al,s,sl,p,pc,pl); }
