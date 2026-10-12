/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_AEAD_H
#define RKLINGS_AEAD_H
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
#endif
enum { RK_AEAD_KEY_LENGTH=32, RK_AEAD_NONCE_LENGTH=12, RK_AEAD_TAG_LENGTH=16,
	RK_AEAD_AAD_MAX=32, RK_AEAD_PLAINTEXT_MAX=64,
	RK_AEAD_SEALED_MAX=RK_AEAD_PLAINTEXT_MAX+RK_AEAD_TAG_LENGTH,
	RK_AEAD_WORK_MAX=RK_AEAD_AAD_MAX+RK_AEAD_SEALED_MAX,
	RK_AEAD_MAY_BACKLOG=1, RK_AEAD_MAY_SLEEP=2 };
struct rk_aead_ops {
	void *(*alloc)(void); bool (*is_error)(const void *); int (*pointer_error)(const void *);
	unsigned int (*iv_size)(void *); unsigned int (*auth_size)(void *);
	int (*set_key)(void *,const u8 *,unsigned int); int (*set_auth_size)(void *,unsigned int);
	void *(*request_alloc)(void *); void (*request_free)(void *); void (*tfm_free)(void *);
	void (*sg_init_one)(void *,void *,unsigned int); void (*wait_init)(void *);
	void (*set_callback)(void *,unsigned int,void *); void (*set_ad)(void *,unsigned int);
	void (*set_crypt)(void *,void *,unsigned int,u8 *); int (*encrypt)(void *); int (*decrypt)(void *);
	int (*wait_request)(int,void *); void (*clear)(void *,size_t);
};
int rk_aead_seal_with(const struct rk_aead_ops *,const u8 *,const u8 *,const u8 *,size_t,const u8 *,size_t,u8 *,size_t,size_t *);
int rk_aead_open_with(const struct rk_aead_ops *,const u8 *,const u8 *,const u8 *,size_t,const u8 *,size_t,u8 *,size_t,size_t *);
int rk_aead_seal(const u8 *,const u8 *,const u8 *,size_t,const u8 *,size_t,u8 *,size_t,size_t *);
int rk_aead_open(const u8 *,const u8 *,const u8 *,size_t,const u8 *,size_t,u8 *,size_t,size_t *);
extern const struct rk_aead_ops rk_kernel_aead_ops;
#endif
