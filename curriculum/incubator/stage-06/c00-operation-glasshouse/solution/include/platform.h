/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_PLATFORM_H
#define ROOTKITLINGS_PLATFORM_H

#include "authenticated.h"
#include "transaction.h"

enum rk_platform_step {
	RK_STEP_NONE,
	RK_STEP_OPEN_TARGET,
	RK_STEP_READ,
	RK_STEP_CREATE_CANDIDATE,
	RK_STEP_WRITE,
	RK_STEP_SYNC_CANDIDATE,
	RK_STEP_REOPEN_TARGET,
	RK_STEP_REOPEN_SIBLING,
	RK_STEP_EXCHANGE_APPLY,
	RK_STEP_SYNC_APPLY,
	RK_STEP_PUBLISH,
	RK_STEP_EXCHANGE_RESTORE,
	RK_STEP_SYNC_RESTORE,
	RK_STEP_UNLINK,
	RK_STEP_SYNC_UNLINK,
};

int rk_platform_prepare(void);
void rk_platform_finish(void);
int rk_platform_open_target(struct rk_object *object, enum rk_platform_step step);
int rk_platform_create_candidate(struct rk_object *object);
int rk_platform_open_sibling(struct rk_object *object,
			     enum rk_platform_step step);
void rk_platform_close(struct rk_object *object);
long rk_platform_read(struct rk_object *object, u8 *buffer, size_t length,
		      size_t *position);
long rk_platform_write(struct rk_object *object, const u8 *buffer,
		       size_t length, size_t *position);
int rk_platform_sync_file(struct rk_object *object);
int rk_platform_exchange(struct rk_object *target, struct rk_object *sibling,
			 enum rk_platform_step step);
int rk_platform_sync_directory(enum rk_platform_step step);
int rk_platform_unlink_sibling(struct rk_object *expected);
bool rk_platform_should_fail(enum rk_platform_step step);
void rk_platform_warn_preserved(void);

int rk_platform_auth_create(void **context, const u8 *key, size_t key_length);
void rk_platform_auth_destroy(void *context);
int rk_platform_hmac(void *context, const u8 *data, size_t length,
		     u8 digest[RK_AUTH_DIGEST_LENGTH]);
int rk_platform_sign_once(const u8 *key, size_t key_length, const u8 *data,
			  size_t length, u8 digest[RK_AUTH_DIGEST_LENGTH]);
bool rk_platform_tags_equal(const u8 *left, const u8 *right, size_t length);
void rk_platform_lock(void *context);
void rk_platform_unlock(void *context);
void rk_platform_clear(void *buffer, size_t length);

#endif
