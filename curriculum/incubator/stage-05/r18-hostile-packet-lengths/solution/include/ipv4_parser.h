/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_IPV4_PARSER_H
#define ROOTKITLINGS_IPV4_PARSER_H

#ifdef __KERNEL__
#include <linux/stddef.h>
#include <linux/types.h>
#else
#include <stddef.h>
#endif

enum rk_ipv4_parse_result {
	RK_IPV4_PARSE_OK,
	RK_IPV4_PARSE_TRUNCATED,
	RK_IPV4_PARSE_MALFORMED,
	RK_IPV4_PARSE_FRAGMENTED,
};

struct rk_ipv4_view {
	size_t header_length;
	size_t packet_length;
	size_t payload_offset;
	size_t payload_length;
	unsigned char protocol;
};

enum rk_ipv4_parse_result
rk_parse_ipv4(const unsigned char *packet, size_t available,
	      struct rk_ipv4_view *view);

#endif
