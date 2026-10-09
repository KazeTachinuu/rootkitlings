// SPDX-License-Identifier: GPL-2.0
#include "udp_classifier.h"

enum {
	RK_IPV4_PROTOCOL_UDP = 17,
	RK_UDP_HEADER_LENGTH = 8,
};

static unsigned int rk_read_be16(const unsigned char *bytes)
{
	return ((unsigned int)bytes[0] << 8) | bytes[1];
}

enum rk_udp_classify_result
rk_classify_ipv4_udp(const struct rk_ipv4_view *ipv4,
		     const unsigned char *udp_header,
		     size_t udp_header_length, unsigned int target_port,
		     struct rk_udp_view *view)
{
	struct rk_udp_view parsed;
	unsigned int datagram_length;

	if (!ipv4 || !view || target_port > 65535)
		return RK_UDP_MALFORMED;
	if (ipv4->protocol != RK_IPV4_PROTOCOL_UDP)
		return RK_UDP_OTHER_PROTOCOL;
	if (ipv4->payload_length < RK_UDP_HEADER_LENGTH)
		return RK_UDP_MALFORMED;
	if (!udp_header)
		return RK_UDP_MALFORMED;
	if (udp_header_length < RK_UDP_HEADER_LENGTH)
		return RK_UDP_TRUNCATED;

	parsed.source_port = rk_read_be16(udp_header);
	parsed.destination_port = rk_read_be16(udp_header + 2);
	datagram_length = rk_read_be16(udp_header + 4);
	if (datagram_length < RK_UDP_HEADER_LENGTH ||
	    datagram_length > ipv4->payload_length)
		return RK_UDP_MALFORMED;
	if (parsed.destination_port != target_port)
		return RK_UDP_OTHER_PORT;

	parsed.datagram_offset = ipv4->payload_offset;
	parsed.datagram_length = datagram_length;
	parsed.payload_offset = ipv4->payload_offset + RK_UDP_HEADER_LENGTH;
	parsed.payload_length = datagram_length - RK_UDP_HEADER_LENGTH;
	parsed.trailing_length = ipv4->payload_length - datagram_length;
	*view = parsed;
	return RK_UDP_MATCH;
}
