// SPDX-License-Identifier: GPL-2.0
#include "udp_redirect.h"

enum rk_udp_redirect_result
rk_redirect_ipv4_udp(struct sk_buff *skb, const struct rk_udp_view *view,
		     unsigned int match_port, unsigned int replacement_port)
{
	/* TODO: validate the view, make the UDP header writable, and redirect it. */
	(void)skb;
	(void)view;
	(void)match_port;
	(void)replacement_port;
	return RK_UDP_REDIRECT_INVALID_ARGUMENT;
}
