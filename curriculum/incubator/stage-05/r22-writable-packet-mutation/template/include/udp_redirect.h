/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_UDP_REDIRECT_H
#define ROOTKITLINGS_UDP_REDIRECT_H

#include "udp_classifier.h"

struct sk_buff;

enum rk_udp_redirect_result {
	RK_UDP_REDIRECT_OK,
	RK_UDP_REDIRECT_INVALID_ARGUMENT,
	RK_UDP_REDIRECT_INVALID_GEOMETRY,
	RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	RK_UDP_REDIRECT_STALE_VIEW,
	RK_UDP_REDIRECT_NOT_WRITABLE,
};

enum rk_udp_redirect_result
rk_redirect_ipv4_udp(struct sk_buff *skb, const struct rk_udp_view *view,
		     unsigned int match_port, unsigned int replacement_port);

#endif
