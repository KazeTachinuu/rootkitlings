/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_AEAD_PLATFORM_H
#define RKLINGS_AEAD_PLATFORM_H
#include "aead.h"
#include <crypto/aead.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
typedef struct scatterlist rk_scatterlist;
typedef struct crypto_wait rk_crypto_wait;
#endif
