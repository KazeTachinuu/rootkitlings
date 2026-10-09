/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_DNS_TRANSPORT_H
#define ROOTKITLINGS_DNS_TRANSPORT_H

#include "authenticated.h"

int rk_udp_send_loopback(const u8 *packet, size_t length);

#endif
