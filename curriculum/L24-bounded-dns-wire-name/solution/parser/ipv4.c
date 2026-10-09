// SPDX-License-Identifier: GPL-2.0
#include "ipv4_parser.h"

enum {
	RK_IPV4_MIN_HEADER = 20,
	RK_IPV4_MORE_FRAGMENTS = 0x2000,
	RK_IPV4_FRAGMENT_OFFSET = 0x1fff,
};

static unsigned int rk_read_be16(const unsigned char *bytes)
{
	return ((unsigned int)bytes[0] << 8) | bytes[1];
}

enum rk_ipv4_parse_result
rk_parse_ipv4(const unsigned char *packet, size_t available,
	      struct rk_ipv4_view *view)
{
	struct rk_ipv4_view parsed;
	unsigned int header_words;
	unsigned int fragment;
	size_t header_length;
	size_t packet_length;

	if (!packet || !view)
		return RK_IPV4_PARSE_MALFORMED;
	if (available < RK_IPV4_MIN_HEADER)
		return RK_IPV4_PARSE_TRUNCATED;
	if ((packet[0] >> 4) != 4)
		return RK_IPV4_PARSE_MALFORMED;
	header_words = packet[0] & 0x0f;
	if (header_words < 5)
		return RK_IPV4_PARSE_MALFORMED;
	header_length = header_words * 4;
	if (header_length > available)
		return RK_IPV4_PARSE_TRUNCATED;

	packet_length = rk_read_be16(packet + 2);
	if (packet_length < header_length)
		return RK_IPV4_PARSE_MALFORMED;
	if (packet_length > available)
		return RK_IPV4_PARSE_TRUNCATED;

	fragment = rk_read_be16(packet + 6);
	if (fragment & (RK_IPV4_MORE_FRAGMENTS | RK_IPV4_FRAGMENT_OFFSET))
		return RK_IPV4_PARSE_FRAGMENTED;

	parsed.header_length = header_length;
	parsed.packet_length = packet_length;
	parsed.payload_offset = header_length;
	parsed.payload_length = packet_length - header_length;
	parsed.protocol = packet[9];
	*view = parsed;
	return RK_IPV4_PARSE_OK;
}
