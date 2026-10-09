/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_STATUS_SIGNAL_H
#define ROOTKITLINGS_STATUS_SIGNAL_H

#include "authenticated.h"

#define RK_FAIL_STATUS_SIGNAL 14

int rk_status_signal_send(const u8 *key, size_t key_length, u64 epoch,
			  u64 sequence, u8 status, u16 transaction_id);

#endif
