// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipv4_parser.h"

enum { PACKET_CAPACITY = 65536 };

static unsigned int assertions;

static void fail(const char *label, const char *detail)
{
	fprintf(stderr, "[x] %s: %s\n", label, detail);
	exit(EXIT_FAILURE);
}

static void check(int condition, const char *label, const char *detail)
{
	assertions++;
	if (!condition)
		fail(label, detail);
}

static struct rk_ipv4_view sentinel(void)
{
	return (struct rk_ipv4_view) {
		.header_length = 101,
		.packet_length = 202,
		.payload_offset = 303,
		.payload_length = 404,
		.protocol = 0xa5,
	};
}

static int is_sentinel(const struct rk_ipv4_view *view)
{
	return view->header_length == 101 && view->packet_length == 202 &&
	       view->payload_offset == 303 &&
	       view->payload_length == 404 && view->protocol == 0xa5;
}

static void write_be16(unsigned char *bytes, unsigned int value)
{
	bytes[0] = (unsigned char)(value >> 8);
	bytes[1] = (unsigned char)value;
}

static void make_packet(unsigned char *packet, unsigned int version,
			unsigned int header_words, unsigned int packet_length,
			unsigned int fragment, unsigned int protocol)
{
	memset(packet, 0x6d, PACKET_CAPACITY);
	packet[0] = (unsigned char)((version << 4) | header_words);
	write_be16(packet + 2, packet_length);
	write_be16(packet + 6, fragment);
	packet[8] = 64;
	packet[9] = (unsigned char)protocol;
}

static void expect_failure(const char *label, const unsigned char *packet,
			   size_t available,
			   enum rk_ipv4_parse_result expected)
{
	struct rk_ipv4_view view = sentinel();
	enum rk_ipv4_parse_result result;

	result = rk_parse_ipv4(packet, available, &view);
	check(result == expected, label, "wrong result");
	check(is_sentinel(&view), label, "failure changed the output view");
}

static void expect_success(const char *label, const unsigned char *packet,
			   size_t available, size_t header_length,
			   size_t packet_length, unsigned int protocol)
{
	struct rk_ipv4_view view = sentinel();
	enum rk_ipv4_parse_result result;

	result = rk_parse_ipv4(packet, available, &view);
	check(result == RK_IPV4_PARSE_OK, label, "valid packet was rejected");
	check(view.header_length == header_length, label,
	      "wrong header length");
	check(view.packet_length == packet_length, label,
	      "wrong packet length");
	check(view.payload_offset == header_length, label,
	      "wrong payload offset");
	check(view.payload_length == packet_length - header_length, label,
	      "wrong payload length");
	check(view.protocol == protocol, label, "wrong protocol");
}

static void test_examples(unsigned char *packet)
{
	unsigned char *fixed_prefix;
	unsigned char *unaligned;

	make_packet(packet, 4, 5, 20, 0, 6);
	expect_success("minimum header", packet, 20, 20, 20, 6);

	make_packet(packet, 4, 5, 27, 0, 17);
	expect_success("payload", packet, 27, 20, 27, 17);

	make_packet(packet, 4, 6, 31, 0, 1);
	expect_success("options", packet, 31, 24, 31, 1);

	make_packet(packet, 4, 5, 20, 0, 6);
	expect_success("capture padding", packet, 4096, 20, 20, 6);
	fixed_prefix = malloc(20);
	check(fixed_prefix != NULL, "fixed prefix", "allocation failed");
	memcpy(fixed_prefix, packet, 20);
	fixed_prefix[0] = 0x4f;
	write_be16(fixed_prefix + 2, 0xffff);
	expect_success("fixed prefix only", fixed_prefix, 0xffff, 60,
		       0xffff, 6);
	free(fixed_prefix);

	make_packet(packet, 4, 5, 20, 0x4000, 6);
	expect_success("do-not-fragment", packet, 20, 20, 20, 6);
	make_packet(packet, 4, 5, 20, 0x8000, 6);
	expect_success("Linux IP_CE flag name", packet, 20, 20, 20, 6);

	unaligned = malloc(PACKET_CAPACITY + 1);
	check(unaligned != NULL, "unaligned", "allocation failed");
	make_packet(unaligned + 1, 4, 7, 33, 0, 132);
	expect_success("unaligned base", unaligned + 1, 33, 28, 33, 132);
	free(unaligned);
}

static void test_failures(unsigned char *packet)
{
	struct rk_ipv4_view view = sentinel();
	size_t available;

	check(rk_parse_ipv4(NULL, 20, &view) == RK_IPV4_PARSE_MALFORMED,
	      "null packet", "wrong result");
	check(is_sentinel(&view), "null packet", "changed the output view");
	make_packet(packet, 4, 5, 20, 0, 6);
	check(rk_parse_ipv4(packet, 20, NULL) == RK_IPV4_PARSE_MALFORMED,
	      "null view", "wrong result");

	for (available = 0; available < 20; available++)
		expect_failure("fixed header truncation", packet, available,
			       RK_IPV4_PARSE_TRUNCATED);

	make_packet(packet, 6, 5, 20, 0, 6);
	expect_failure("wrong version", packet, 20, RK_IPV4_PARSE_MALFORMED);
	make_packet(packet, 4, 4, 20, 0, 6);
	expect_failure("short IHL", packet, 20, RK_IPV4_PARSE_MALFORMED);
	make_packet(packet, 4, 6, 24, 0, 6);
	expect_failure("option truncation", packet, 23, RK_IPV4_PARSE_TRUNCATED);
	make_packet(packet, 4, 6, 23, 0, 6);
	expect_failure("total shorter than header", packet, 24,
		       RK_IPV4_PARSE_MALFORMED);
	make_packet(packet, 4, 5, 21, 0, 6);
	expect_failure("declared packet truncation", packet, 20,
		       RK_IPV4_PARSE_TRUNCATED);
	make_packet(packet, 4, 5, 20, 0x2000, 6);
	expect_failure("first fragment", packet, 20, RK_IPV4_PARSE_FRAGMENTED);
	make_packet(packet, 4, 5, 20, 0x0001, 6);
	expect_failure("later fragment", packet, 20, RK_IPV4_PARSE_FRAGMENTED);
}

static void test_result_precedence(unsigned char *packet)
{
	make_packet(packet, 6, 4, 0, 0x2000, 6);
	expect_failure("unavailable fixed prefix wins", packet, 19,
		       RK_IPV4_PARSE_TRUNCATED);
	expect_failure("version wins after fixed prefix", packet, 20,
		       RK_IPV4_PARSE_MALFORMED);

	make_packet(packet, 4, 4, 0, 0x2000, 6);
	expect_failure("IHL format wins", packet, 20,
		       RK_IPV4_PARSE_MALFORMED);

	make_packet(packet, 4, 15, 20, 0x2000, 6);
	expect_failure("IHL boundary wins", packet, 20,
		       RK_IPV4_PARSE_TRUNCATED);

	make_packet(packet, 4, 15, 59, 0x2000, 6);
	expect_failure("total format before fragment", packet, 60,
		       RK_IPV4_PARSE_MALFORMED);

	make_packet(packet, 4, 5, 21, 0x2000, 6);
	expect_failure("total boundary before fragment", packet, 20,
		       RK_IPV4_PARSE_TRUNCATED);
}

static void test_first_octet(unsigned char *packet)
{
	unsigned int value;

	for (value = 0; value <= 0xff; value++) {
		enum rk_ipv4_parse_result expected;
		unsigned int version = value >> 4;
		unsigned int words = value & 0x0f;
		struct rk_ipv4_view view = sentinel();

		make_packet(packet, version, words, 60, 0, 17);
		expected = version == 4 && words >= 5 ? RK_IPV4_PARSE_OK :
			   RK_IPV4_PARSE_MALFORMED;
		check(rk_parse_ipv4(packet, 60, &view) == expected,
		      "first octet sweep", "wrong classification");
		if (expected == RK_IPV4_PARSE_OK) {
			check(view.header_length == words * 4,
			      "first octet sweep", "wrong IHL conversion");
			check(view.payload_length == 60 - words * 4,
			      "first octet sweep", "wrong payload boundary");
		} else {
			check(is_sentinel(&view), "first octet sweep",
			      "failure changed the output view");
		}
	}
}

static void test_total_lengths(unsigned char *packet)
{
	unsigned int total;

	make_packet(packet, 4, 5, 20, 0, 17);
	for (total = 0; total <= 0xffff; total++) {
		struct rk_ipv4_view view = sentinel();
		enum rk_ipv4_parse_result expected = total < 20 ?
			RK_IPV4_PARSE_MALFORMED : RK_IPV4_PARSE_OK;

		write_be16(packet + 2, total);
		check(rk_parse_ipv4(packet, PACKET_CAPACITY, &view) == expected,
		      "total length sweep", "wrong classification");
		if (expected == RK_IPV4_PARSE_OK) {
			check(view.packet_length == total, "total length sweep",
			      "wrong byte order");
			check(view.payload_length == total - 20,
			      "total length sweep", "wrong payload length");
		} else {
			check(is_sentinel(&view), "total length sweep",
			      "failure changed the output view");
		}
	}
}

static void test_fragment_field(unsigned char *packet)
{
	unsigned int fragment;

	make_packet(packet, 4, 5, 20, 0, 17);
	for (fragment = 0; fragment <= 0xffff; fragment++) {
		struct rk_ipv4_view view = sentinel();
		enum rk_ipv4_parse_result expected;

		write_be16(packet + 6, fragment);
		if (fragment & 0x3fff)
			expected = RK_IPV4_PARSE_FRAGMENTED;
		else
			expected = RK_IPV4_PARSE_OK;
		check(rk_parse_ipv4(packet, 20, &view) == expected,
		      "fragment field sweep", "wrong classification");
		if (expected != RK_IPV4_PARSE_OK)
			check(is_sentinel(&view), "fragment field sweep",
			      "failure changed the output view");
	}
}

static void test_protocols_and_bounds(unsigned char *packet)
{
	unsigned int protocol;
	size_t available;

	for (protocol = 0; protocol <= 0xff; protocol++) {
		make_packet(packet, 4, 5, 20, 0, protocol);
		expect_success("protocol sweep", packet, 20, 20, 20,
			       protocol);
	}

	make_packet(packet, 4, 15, 60, 0, 17);
	for (available = 20; available < 60; available++)
		expect_failure("IHL availability", packet, available,
			       RK_IPV4_PARSE_TRUNCATED);
	expect_success("maximum IHL", packet, 60, 60, 60, 17);

	make_packet(packet, 4, 5, 60, 0, 17);
	for (available = 20; available < 60; available++)
		expect_failure("total availability", packet, available,
			       RK_IPV4_PARSE_TRUNCATED);
}

int main(void)
{
	unsigned char *packet = malloc(PACKET_CAPACITY);

	check(packet != NULL, "fixture", "allocation failed");
	test_examples(packet);
	test_failures(packet);
	test_result_precedence(packet);
	test_first_octet(packet);
	test_total_lengths(packet);
	test_fragment_field(packet);
	test_protocols_and_bounds(packet);
	free(packet);
	printf("[+] IPv4 parser: %u assertions\n", assertions);
	return EXIT_SUCCESS;
}
