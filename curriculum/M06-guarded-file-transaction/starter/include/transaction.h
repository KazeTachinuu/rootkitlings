/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_TRANSACTION_H
#define ROOTKITLINGS_TRANSACTION_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint64_t u64;
#endif

enum {
	RK_FILE_MAX = 256,
	RK_INTENDED_LENGTH = 13,
};

enum rk_transaction_state {
	RK_TRANSACTION_IDLE,
	RK_TRANSACTION_APPLIED,
	RK_TRANSACTION_RESTORED,
	RK_TRANSACTION_PRESERVED,
};

struct rk_object {
	void *handle;
	u64 identity;
	u64 length;
	unsigned int mode;
	unsigned int links;
	unsigned int uid;
	unsigned int gid;
};

struct rk_snapshot {
	struct rk_object retained;
	u8 bytes[RK_FILE_MAX];
	size_t length;
};

struct rk_transaction {
	struct rk_snapshot original;
	struct rk_snapshot candidate;
	enum rk_transaction_state state;
	int last_error;
	bool have_original;
	bool have_candidate;
	bool exchanged;
	bool warned;
};

int rk_transaction_apply(struct rk_transaction *transaction);
int rk_transaction_restore(struct rk_transaction *transaction);
void rk_transaction_dispose(struct rk_transaction *transaction);
const char *rk_transaction_state_name(const struct rk_transaction *transaction);

#endif
