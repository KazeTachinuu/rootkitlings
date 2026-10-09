/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_AEAD_PLATFORM_H
#define ROOTKITLINGS_AEAD_PLATFORM_H

#include "aead.h"

#ifdef __KERNEL__
#include <crypto/aead.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
typedef struct scatterlist rk_scatterlist;
typedef struct crypto_wait rk_crypto_wait;
#else
struct rk_scatterlist { unsigned long words[8]; };
struct rk_crypto_wait { unsigned long words[8]; };
typedef struct rk_scatterlist rk_scatterlist;
typedef struct rk_crypto_wait rk_crypto_wait;
#endif

#endif
