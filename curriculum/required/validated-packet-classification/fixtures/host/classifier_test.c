// SPDX-License-Identifier: GPL-2.0
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "udp_classifier.h"

static unsigned long assertions;

static int expect(int condition, const char *label)
{
	assertions++;
	if (condition)
		return 0;
	fprintf(stderr, "[x] classifier test failed: %s\n", label);
	return 1;
}

static struct rk_ipv4_view ipv4_view_with_header(size_t header_length,
					 size_t payload_length,
					 unsigned char protocol)
{
	static unsigned char packet[65535];
	struct rk_ipv4_view view;
	size_t packet_length;

	if (header_length < 20 || header_length > 60 || header_length % 4 ||
	    payload_length > sizeof(packet) - header_length) {
		fprintf(stderr, "invalid test IPv4 payload length: %zu\n",
			payload_length);
		exit(EXIT_FAILURE);
	}
	packet_length = header_length + payload_length;
	memset(packet, 0, packet_length);
	packet[0] = 0x40 | (header_length / 4);
	packet[2] = packet_length >> 8;
	packet[3] = packet_length;
	packet[9] = protocol;
	if (rk_parse_ipv4(packet, packet_length, &view) != RK_IPV4_PARSE_OK) {
		fprintf(stderr, "accepted IPv4 parser rejected a test view\n");
		exit(EXIT_FAILURE);
	}

	return view;
}

static struct rk_ipv4_view ipv4_view(size_t payload_length,
				     unsigned char protocol)
{
	return ipv4_view_with_header(20, payload_length, protocol);
}

static void make_udp(unsigned char header[8], unsigned int source,
		     unsigned int destination, unsigned int length,
		     unsigned int checksum)
{
	header[0] = source >> 8;
	header[1] = source;
	header[2] = destination >> 8;
	header[3] = destination;
	header[4] = length >> 8;
	header[5] = length;
	header[6] = checksum >> 8;
	header[7] = checksum;
}

static void fill_sentinel(struct rk_udp_view *view)
{
	memset(view, 0xa5, sizeof(*view));
}

static int unchanged(const struct rk_udp_view *view)
{
	struct rk_udp_view sentinel;

	fill_sentinel(&sentinel);
	return memcmp(view, &sentinel, sizeof(*view)) == 0;
}

static int test_precedence(void)
{
	unsigned char udp[8];
	struct rk_ipv4_view ipv4 = ipv4_view(8, 17);
	struct rk_udp_view view;
	long page_size;
	void *guard;
	unsigned int protocol;
	size_t length;

	make_udp(udp, 1, 47021, 8, 0);
	fill_sentinel(&view);
	if (expect(rk_classify_ipv4_udp(NULL, udp, sizeof(udp), 47021,
					&view) == RK_UDP_MALFORMED,
		   "null IPv4 view is malformed") ||
	    expect(unchanged(&view), "null IPv4 view leaves output") ||
	    expect(rk_classify_ipv4_udp(&ipv4, udp, sizeof(udp), 65536,
					&view) == RK_UDP_MALFORMED,
		   "port beyond 16 bits is malformed") ||
	    expect(unchanged(&view), "invalid target leaves output") ||
	    expect(rk_classify_ipv4_udp(&ipv4, udp, sizeof(udp), 47021,
					NULL) == RK_UDP_MALFORMED,
		   "null output view is malformed"))
		return 1;
	ipv4 = ipv4_view(8, 6);
	if (expect(rk_classify_ipv4_udp(&ipv4, udp, sizeof(udp), 65536,
					&view) == RK_UDP_MALFORMED,
		   "invalid target precedes the protocol check") ||
	    expect(rk_classify_ipv4_udp(&ipv4, udp, sizeof(udp), 47021,
					NULL) == RK_UDP_MALFORMED,
		   "null output precedes the protocol check"))
		return 1;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		perror("sysconf pagesize");
		return 1;
	}
	guard = mmap(NULL, (size_t)page_size, PROT_NONE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (guard == MAP_FAILED) {
		perror("mmap guard page");
		return 1;
	}
	fill_sentinel(&view);
	if (expect(rk_classify_ipv4_udp(&ipv4, guard, 8, 47021, &view) ==
		   RK_UDP_OTHER_PROTOCOL,
		   "other protocols do not inspect the UDP buffer") ||
	    expect(unchanged(&view), "guarded other protocol leaves output")) {
		munmap(guard, (size_t)page_size);
		return 1;
	}
	if (munmap(guard, (size_t)page_size)) {
		perror("munmap guard page");
		return 1;
	}

	for (protocol = 0; protocol <= 255; protocol++) {
		if (protocol == 17)
			continue;
		ipv4 = ipv4_view(0, protocol);
		fill_sentinel(&view);
		if (expect(rk_classify_ipv4_udp(&ipv4, NULL, 0, 47021,
						&view) == RK_UDP_OTHER_PROTOCOL,
			   "non-UDP precedes payload and buffer checks") ||
		    expect(unchanged(&view), "other protocol leaves output"))
			return 1;
	}

	for (length = 0; length < 8; length++) {
		ipv4 = ipv4_view(length, 17);
		fill_sentinel(&view);
		if (expect(rk_classify_ipv4_udp(&ipv4, NULL, 0, 47021,
						&view) == RK_UDP_MALFORMED,
			   "short IPv4 payload is malformed") ||
		    expect(unchanged(&view), "short payload leaves output"))
			return 1;
	}

	ipv4 = ipv4_view(8, 17);
	fill_sentinel(&view);
	if (expect(rk_classify_ipv4_udp(&ipv4, NULL, 0, 47021, &view) ==
		   RK_UDP_MALFORMED,
		   "null UDP buffer precedes its zero extent") ||
	    expect(unchanged(&view), "null zero-length buffer leaves output") ||
	    expect(rk_classify_ipv4_udp(&ipv4, NULL, 8, 47021, &view) ==
		   RK_UDP_MALFORMED, "null UDP buffer is malformed") ||
	    expect(unchanged(&view), "null buffer leaves output"))
		return 1;
	for (length = 0; length < 8; length++) {
		fill_sentinel(&view);
		if (expect(rk_classify_ipv4_udp(&ipv4, udp, length, 47021,
						&view) == RK_UDP_TRUNCATED,
			   "short UDP buffer is truncated") ||
		    expect(unchanged(&view), "short buffer leaves output"))
			return 1;
	}
	return 0;
}

static int test_lengths(void)
{
	unsigned char storage[9];
	unsigned char *udp = storage + 1;
	struct rk_ipv4_view ipv4 = ipv4_view(65515, 17);
	struct rk_udp_view view;
	unsigned int length;

	for (length = 0; length <= 65535; length++) {
		make_udp(udp, 0x1234, 47021, length, 0xbeef);
		fill_sentinel(&view);
		if (length < 8) {
			if (expect(rk_classify_ipv4_udp(&ipv4, udp, 8, 47021,
							&view) == RK_UDP_MALFORMED,
				   "UDP length below header is malformed") ||
			    expect(unchanged(&view),
				   "malformed UDP length leaves output"))
				return 1;
		} else if (length > 65515) {
			if (expect(rk_classify_ipv4_udp(&ipv4, udp, 8, 47021,
							&view) == RK_UDP_MALFORMED,
				   "UDP length beyond a valid IPv4 payload is malformed") ||
			    expect(unchanged(&view),
				   "oversize UDP length leaves output unchanged"))
				return 1;
		} else if (expect(rk_classify_ipv4_udp(&ipv4, udp, 8, 47021,
						       &view) == RK_UDP_MATCH,
				  "every IPv4-bounded UDP length matches") ||
			   expect(view.datagram_length == length,
				  "view keeps the UDP datagram length") ||
			   expect(view.payload_length == length - 8,
				  "view subtracts only after validation") ||
			   expect(view.trailing_length == 65515U - length,
				  "view records bytes after the datagram")) {
			return 1;
		}
	}

	for (length = 8; length <= 65515; length++) {
		ipv4 = ipv4_view(length, 17);
		make_udp(udp, 0x1234, 47021, length, 0);
		if (expect(rk_classify_ipv4_udp(&ipv4, udp, 8, 47021,
						&view) == RK_UDP_MATCH,
			   "UDP length may equal every IPv4 payload bound") ||
		    expect(view.trailing_length == 0,
			   "equal bounds leave no trailing bytes"))
			return 1;
	}

	ipv4 = ipv4_view(8, 17);
	make_udp(udp, 1, 47021, 9, 0);
	fill_sentinel(&view);
	if (expect(rk_classify_ipv4_udp(&ipv4, udp, 8, 1, &view) ==
		   RK_UDP_MALFORMED,
		   "oversize UDP length precedes port mismatch") ||
	    expect(unchanged(&view), "oversize UDP length leaves output"))
		return 1;
	make_udp(udp, 1, 47021, 7, 0);
	fill_sentinel(&view);
	if (expect(rk_classify_ipv4_udp(&ipv4, udp, 8, 1, &view) ==
		   RK_UDP_MALFORMED,
		   "short UDP length precedes port mismatch") ||
	    expect(unchanged(&view), "short UDP length leaves output"))
		return 1;
	return 0;
}

static int test_ports_checksum_and_view(void)
{
	unsigned char storage[9];
	unsigned char *udp = storage + 1;
	struct rk_ipv4_view ipv4 = ipv4_view(12, 17);
	struct rk_udp_view view;
	unsigned int value;

	for (value = 0; value <= 65535; value++) {
		make_udp(udp, 0xabcd, value, 8, value ^ 0x55aa);
		if (expect(rk_classify_ipv4_udp(&ipv4, udp, sizeof(udp),
						value, &view) == RK_UDP_MATCH,
			   "every 16-bit destination port can match") ||
		    expect(view.source_port == 0xabcd &&
			   view.destination_port == value,
			   "ports decode bytewise in network order") ||
		    expect(view.datagram_offset == 20 &&
			   view.datagram_length == 8 &&
			   view.payload_offset == 28 &&
			   view.payload_length == 0 &&
			   view.trailing_length == 4,
			   "matched view has exact bounded offsets"))
			return 1;
	}
	for (value = 0; value <= 65535; value++) {
		make_udp(udp, value, 47021, 8, value ^ 0xaa55);
		if (expect(rk_classify_ipv4_udp(&ipv4, udp, sizeof(udp),
						47021, &view) == RK_UDP_MATCH,
			   "every 16-bit source port is decoded") ||
		    expect(view.source_port == value &&
			   view.destination_port == 47021,
			   "source and destination ports remain distinct"))
			return 1;
	}
	for (value = 0; value <= 65535; value++) {
		make_udp(udp, 0x1234, 47021, 8, value);
		if (expect(rk_classify_ipv4_udp(&ipv4, udp, 8, 47021,
						&view) == RK_UDP_MATCH,
			   "every checksum field has the same structural result") ||
		    expect(view.source_port == 0x1234 &&
			   view.destination_port == 47021,
			   "checksum bytes do not alter decoded ports"))
			return 1;
	}

	make_udp(udp, 1, 2, 8, 0xffff);
	fill_sentinel(&view);
	if (expect(rk_classify_ipv4_udp(&ipv4, udp, sizeof(udp), 3, &view) ==
		   RK_UDP_OTHER_PORT, "different destination is other port") ||
	    expect(unchanged(&view), "other port leaves output") ||
	    expect(rk_classify_ipv4_udp(&ipv4, udp, SIZE_MAX, 2, &view) ==
		   RK_UDP_MATCH, "buffer extent may exceed the fixed header") ||
	    expect(view.trailing_length == 4,
		   "surplus IPv4 payload remains explicit"))
		return 1;
	return 0;
}

static int test_ipv4_header_lengths(void)
{
	unsigned char storage[9];
	unsigned char *udp = storage + 1;
	struct rk_ipv4_view ipv4;
	struct rk_udp_view view;
	size_t header_length;

	make_udp(udp, 0x1234, 47021, 9, 0);
	for (header_length = 20; header_length <= 60; header_length += 4) {
		ipv4 = ipv4_view_with_header(header_length, 13, 17);
		if (expect(rk_classify_ipv4_udp(&ipv4, udp, 8, 47021,
						&view) == RK_UDP_MATCH,
			   "every valid IPv4 header length composes") ||
		    expect(view.datagram_offset == header_length &&
			   view.payload_offset == header_length + 8,
			   "UDP offsets follow the validated IPv4 view") ||
		    expect(view.datagram_length == 9 &&
			   view.payload_length == 1 &&
			   view.trailing_length == 4,
			   "lengths remain relative to the IPv4 payload"))
			return 1;
	}
	return 0;
}

int main(void)
{
	if (test_precedence() || test_lengths() || test_ports_checksum_and_view() ||
	    test_ipv4_header_lengths())
		return 1;
	printf("[+] classifier host tests passed: %lu assertions\n", assertions);
	return 0;
}
