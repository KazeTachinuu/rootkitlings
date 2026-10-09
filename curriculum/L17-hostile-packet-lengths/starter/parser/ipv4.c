// SPDX-License-Identifier: GPL-2.0
#include "ipv4_parser.h"

enum rk_ipv4_parse_result
rk_parse_ipv4(const unsigned char *packet, size_t available,
	      struct rk_ipv4_view *view)
{
	/* Parse and validate one complete, unfragmented IPv4 packet. */
	(void)packet;
	(void)available;
	(void)view;
	return RK_IPV4_PARSE_MALFORMED;
}
