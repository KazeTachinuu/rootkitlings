/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_AUTHENTICATED_DNS_H
#define ROOTKITLINGS_AUTHENTICATED_DNS_H

#include "authenticated.h"
#include "dns_query.h"

int rk_dns_signal_build(const u8 key[RK_AUTH_KEY_LENGTH], size_t key_length,
			u64 epoch, u64 sequence, u8 status, u16 transaction_id,
			u8 *packet, size_t capacity, size_t *used);
int rk_dns_signal_receive(struct rk_receiver *receiver, const u8 *packet,
			  size_t length);

#endif
