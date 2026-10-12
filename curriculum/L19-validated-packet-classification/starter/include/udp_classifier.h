/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_UDP_CLASSIFIER_H
#define RKLINGS_UDP_CLASSIFIER_H

#include "ipv4_parser.h"

enum rk_udp_classify_result {
	RK_UDP_MATCH,
	RK_UDP_OTHER_PROTOCOL,
	RK_UDP_OTHER_PORT,
	RK_UDP_TRUNCATED,
	RK_UDP_MALFORMED,
};

struct rk_udp_view {
	size_t datagram_offset;
	size_t datagram_length;
	size_t payload_offset;
	size_t payload_length;
	size_t trailing_length;
	unsigned int source_port;
	unsigned int destination_port;
};

enum rk_udp_classify_result
rk_classify_ipv4_udp(const struct rk_ipv4_view *ipv4,
		     const unsigned char *udp_header,
		     size_t udp_header_length, unsigned int target_port,
		     struct rk_udp_view *view);

#endif
