// SPDX-License-Identifier: GPL-2.0
#include "udp_classifier.h"

enum rk_udp_classify_result
rk_classify_ipv4_udp(const struct rk_ipv4_view *ipv4,
		     const unsigned char *udp_header,
		     size_t udp_header_length, unsigned int target_port,
		     struct rk_udp_view *view)
{
	(void)ipv4;
	(void)udp_header;
	(void)udp_header_length;
	(void)target_port;
	(void)view;
	return RK_UDP_MALFORMED;
}
