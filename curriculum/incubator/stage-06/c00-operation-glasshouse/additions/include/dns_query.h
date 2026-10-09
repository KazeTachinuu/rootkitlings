/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_DNS_QUERY_H
#define ROOTKITLINGS_DNS_QUERY_H

#include "authenticated.h"

enum {
	RK_DNS_HEADER_LENGTH = 12,
	RK_DNS_SIGNAL_PACKET_LENGTH = 116,
	RK_DNS_SIGNAL_PORT = 5300,
};

int rk_dns_query_encode(const u8 message[RK_AUTH_MESSAGE_LENGTH], u16 id,
			u8 *packet, size_t capacity, size_t *used);
int rk_dns_query_decode(const u8 *packet, size_t length,
			u8 message[RK_AUTH_MESSAGE_LENGTH], u16 *id);

#endif
