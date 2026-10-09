// SPDX-License-Identifier: GPL-2.0
#include <arpa/inet.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "udp_redirect.h"
#include "writable.h"
#include <linux/skbuff.h>
#include <linux/udp.h>

enum {
	MATCH_PORT = 47021,
	REPLACEMENT_PORT = 47022,
	IPV4_LENGTH = 36,
	UDP_OFFSET = 20,
};

struct fixture {
	struct sk_buff skb;
	unsigned char storage[128];
};

static unsigned char relocated[256];
static unsigned long assertions;
static unsigned int writable_calls;
static unsigned int writable_length;
static int writable_error;
static unsigned int relocate_by;
static unsigned int stale_field;
static bool linearize_prefix;
static unsigned int checksum_calls;
static __be16 checksum_from;
static __be16 checksum_to;
static bool checksum_pseudohdr;

static void fail(const char *label, unsigned int line)
{
	fprintf(stderr, "line %u: %s\n", line, label);
	exit(EXIT_FAILURE);
}

#define EXPECT(condition, label) \
	do { assertions++; if (!(condition)) fail((label), __LINE__); } while (0)

static struct udphdr *fixture_udp(struct fixture *fixture)
{
	return (struct udphdr *)(fixture->skb.data +
		fixture->skb.network_offset + UDP_OFFSET);
}

static void reset_controls(void)
{
	writable_calls = 0;
	writable_length = 0;
	writable_error = 0;
	relocate_by = 0;
	stale_field = 0;
	linearize_prefix = false;
	checksum_calls = 0;
	checksum_from = 0;
	checksum_to = 0;
	checksum_pseudohdr = true;
}

static void make_fixture(struct fixture *fixture, unsigned int source_port,
			 unsigned int checksum)
{
	struct udphdr *udp;

	memset(fixture, 0, sizeof(*fixture));
	fixture->skb.data = fixture->storage + 1;
	fixture->skb.wire = fixture->skb.data;
	fixture->skb.network_offset = 3;
	fixture->skb.len = 3 + IPV4_LENGTH;
	fixture->skb.head_length = fixture->skb.len;
	fixture->skb.hash = 0x12345678;
	fixture->skb.sw_hash = 1;
	fixture->skb.l4_hash = 1;
	udp = fixture_udp(fixture);
	udp->source = htons((u16)source_port);
	udp->dest = htons(MATCH_PORT);
	udp->len = htons(16);
	udp->check = htons((u16)checksum);
	memcpy((unsigned char *)udp + sizeof(*udp), "payload!", 8);
	reset_controls();
}

static struct rk_udp_view make_view(unsigned int source_port)
{
	return (struct rk_udp_view) {
		.datagram_offset = UDP_OFFSET,
		.datagram_length = 16,
		.payload_offset = UDP_OFFSET + sizeof(struct udphdr),
		.payload_length = 8,
		.trailing_length = 0,
		.source_port = source_port,
		.destination_port = MATCH_PORT,
	};
}

int rk_skb_make_writable(struct sk_buff *skb, unsigned int length)
{
	unsigned int old_network = (unsigned int)skb->network_offset;
	unsigned int old_len = skb->len;
	struct udphdr *udp;

	writable_calls++;
	writable_length = length;
	if (linearize_prefix && skb->head_length < length) {
		skb->head_length = length;
		skb->data_len = skb->len - length;
	}
	if (relocate_by) {
		memset(relocated, 0xa5, sizeof(relocated));
		memcpy(relocated, skb->wire, old_network);
		memcpy(relocated + old_network + relocate_by,
		       skb->wire + old_network, old_len - old_network);
		skb->data = relocated;
		skb->wire = relocated;
		skb->network_offset += (int)relocate_by;
		skb->len += relocate_by;
		skb->head_length = skb->len;
	}
	udp = (struct udphdr *)(skb->data + skb->network_offset + UDP_OFFSET);
	if (stale_field == 1)
		udp->source ^= htons(1);
	else if (stale_field == 2)
		udp->dest ^= htons(1);
	else if (stale_field == 3)
		udp->len ^= htons(1);
	return writable_error;
}

void rk_skb_fail_next_writable(void)
{
	writable_error = -1;
}

void inet_proto_csum_replace2(__sum16 *sum, struct sk_buff *skb,
			      __be16 from, __be16 to, bool pseudohdr)
{
	checksum_calls++;
	checksum_from = from;
	checksum_to = to;
	checksum_pseudohdr = pseudohdr;
	if (skb->ip_summed != CHECKSUM_PARTIAL) {
		uint32_t value = ((~ntohs(*sum)) & UINT16_MAX) +
			((~ntohs(from)) & UINT16_MAX) + ntohs(to);

		value = (value & UINT16_MAX) + (value >> 16);
		value = (value & UINT16_MAX) + (value >> 16);
		*sum = htons((u16)~value);
	}
}

static void expect_unchanged_header(const unsigned char *before,
				    const struct sk_buff *skb)
{
	EXPECT(!memcmp(before, skb->data + skb->network_offset + UDP_OFFSET, 8),
	       "failure changed UDP header bytes");
}

static void test_checksum_space(void)
{
	struct fixture fixture;
	struct rk_udp_view view = make_view(0x1234);
	struct udphdr *udp;
	unsigned int checksum;

	for (checksum = 0; checksum <= UINT16_MAX; checksum++) {
		make_fixture(&fixture, 0x1234, checksum);
		EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
			REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
		       "valid checksum case was rejected");
		udp = fixture_udp(&fixture);
		EXPECT(ntohs(udp->dest) == REPLACEMENT_PORT,
		       "destination port was not replaced");
		EXPECT(writable_calls == 1 && writable_length ==
		       (unsigned int)fixture.skb.network_offset + UDP_OFFSET + 8,
		       "writable request did not end at the UDP header");
		if (!checksum) {
			EXPECT(checksum_calls == 0 && udp->check == 0,
			       "zero checksum was synthesized");
		} else {
			EXPECT(checksum_calls == 1 &&
			       checksum_from == htons(MATCH_PORT) &&
			       checksum_to == htons(REPLACEMENT_PORT) &&
			       !checksum_pseudohdr,
			       "checksum delta arguments are wrong");
			if (checksum == 1)
				EXPECT(udp->check == CSUM_MANGLED_0,
				       "computed zero was not mangled");
			else
				EXPECT(ntohs(udp->check) == checksum - 1,
				       "checksum was not adjusted exactly once");
		}
		EXPECT(!fixture.skb.hash && !fixture.skb.sw_hash &&
		       !fixture.skb.l4_hash, "success did not clear packet hash");
	}
}

static void test_checksum_modes(void)
{
	const unsigned int modes[] = {
		CHECKSUM_NONE, CHECKSUM_UNNECESSARY, CHECKSUM_COMPLETE,
		CHECKSUM_PARTIAL,
	};
	struct fixture fixture;
	struct rk_udp_view view = make_view(0x1234);
	unsigned int index;

	for (index = 0; index < sizeof(modes) / sizeof(modes[0]); index++) {
		make_fixture(&fixture, 0x1234, 0);
		fixture.skb.ip_summed = modes[index];
		if (modes[index] == CHECKSUM_PARTIAL) {
			fixture.skb.csum_start = fixture.skb.network_offset +
						 UDP_OFFSET;
			fixture.skb.csum_offset = offsetof(struct udphdr, check);
		}
		EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
			REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
		       "checksum mode rejected valid packet");
		EXPECT(!fixture_udp(&fixture)->check,
		       "zero checksum changed in a checksum mode");
		if (modes[index] == CHECKSUM_COMPLETE)
			EXPECT(fixture.skb.ip_summed == CHECKSUM_NONE,
			       "zero checksum did not invalidate complete state");
		if (modes[index] == CHECKSUM_PARTIAL)
			EXPECT(fixture.skb.ip_summed == CHECKSUM_PARTIAL,
			       "zero partial checksum changed offload mode");
		EXPECT(checksum_calls == 0,
		       "zero checksum called the replacement helper");

		make_fixture(&fixture, 0x1234, 0x2222);
		fixture.skb.ip_summed = modes[index];
		if (modes[index] == CHECKSUM_PARTIAL) {
			fixture.skb.csum_start = fixture.skb.network_offset +
						 UDP_OFFSET;
			fixture.skb.csum_offset = offsetof(struct udphdr, check);
		}
		EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
			REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
		       "nonzero checksum mode rejected valid packet");
		EXPECT(checksum_calls == 1 && !checksum_pseudohdr,
		       "nonzero checksum did not use the documented helper");
		if (modes[index] == CHECKSUM_PARTIAL)
			EXPECT(ntohs(fixture_udp(&fixture)->check) == 0x2222,
			       "partial checksum helper changed its seed");
		else
			EXPECT(ntohs(fixture_udp(&fixture)->check) == 0x2221,
			       "complete checksum was not adjusted");
		if (modes[index] == CHECKSUM_COMPLETE)
			EXPECT(fixture.skb.ip_summed == CHECKSUM_COMPLETE,
			       "compensated checksum invalidated complete state");
	}
}

static void test_relocation_and_stale_views(void)
{
	struct fixture fixture;
	struct rk_udp_view view = make_view(0x1234);
	unsigned char before[8];
	unsigned int field;

	make_fixture(&fixture, 0x1234, 0x2222);
	relocate_by = 5;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
		REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
	       "relocated header was not reacquired");
	EXPECT(ntohs(fixture_udp(&fixture)->dest) == REPLACEMENT_PORT,
	       "relocated destination was not changed");
	EXPECT(writable_length == 3 + UDP_OFFSET + 8,
	       "helper length used post-helper geometry");
	EXPECT(checksum_calls == 1,
	       "relocated checksum was not adjusted once");

	make_fixture(&fixture, 0x1234, 0x2222);
	fixture.skb.head_length = fixture.skb.network_offset + UDP_OFFSET + 4;
	fixture.skb.data_len = fixture.skb.len - fixture.skb.head_length;
	linearize_prefix = true;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
		REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
	       "nonlinear header was not made writable");
	EXPECT(fixture.skb.head_length == writable_length &&
	       ntohs(fixture_udp(&fixture)->dest) == REPLACEMENT_PORT,
	       "exact nonlinear prefix was not linearized and changed");

	for (field = 1; field <= 3; field++) {
		make_fixture(&fixture, 0x1234, 0x2222);
		memcpy(before, fixture_udp(&fixture), sizeof(before));
		relocate_by = 5;
		stale_field = field;
		EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
			REPLACEMENT_PORT) == RK_UDP_REDIRECT_STALE_VIEW,
		       "post-helper header change was not stale");
		EXPECT(!checksum_calls, "stale view adjusted checksum");
		EXPECT(fixture.skb.hash == 0x12345678,
		       "stale view cleared hash");
	}

	make_fixture(&fixture, 0x1234, 0x2222);
	memcpy(before, fixture_udp(&fixture), sizeof(before));
	relocate_by = 5;
	writable_error = -1;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
		REPLACEMENT_PORT) == RK_UDP_REDIRECT_NOT_WRITABLE,
	       "helper failure result was lost");
	expect_unchanged_header(before, &fixture.skb);
	EXPECT(fixture.skb.network_offset == 8,
	       "failure fixture did not change geometry");
	EXPECT(!checksum_calls && fixture.skb.hash == 0x12345678,
	       "helper failure changed derived packet state");
}

static void test_invalid_inputs_and_geometry(void)
{
	struct fixture fixture;
	struct rk_udp_view view = make_view(0x1234);
	struct rk_udp_view changed;
	unsigned char before[8];

	make_fixture(&fixture, 0x1234, 0x2222);
	memcpy(before, fixture_udp(&fixture), sizeof(before));
	EXPECT(rk_redirect_ipv4_udp(NULL, &view, MATCH_PORT, REPLACEMENT_PORT) ==
	       RK_UDP_REDIRECT_INVALID_ARGUMENT, "null skb accepted");
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, NULL, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "null view accepted");
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, 0,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "zero match port accepted");
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       MATCH_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "identical ports accepted");
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, 65536,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "oversize match port accepted");
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       65536) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "oversize replacement port accepted");
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       0) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "zero replacement port accepted");

	changed = view;
	changed.source_port = 65536;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "oversize source port accepted");
	changed = view;
	changed.destination_port--;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "view for another destination accepted");
	changed = view;
	changed.datagram_length = 7;
	changed.payload_length = 0;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "datagram shorter than a UDP header accepted");

	changed = view;
	changed.datagram_offset = SIZE_MAX;
	changed.payload_offset = 7;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "size_t offset wrap was not rejected deliberately");
	changed = view;
	changed.datagram_length = SIZE_MAX;
	changed.payload_length = SIZE_MAX - 8;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "oversize datagram view was not rejected");
	changed = view;
	changed.payload_offset++;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "inconsistent payload offset accepted");
	changed = view;
	changed.payload_length++;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_ARGUMENT,
	       "inconsistent payload length accepted");
	EXPECT(!writable_calls && !checksum_calls &&
	       fixture.skb.hash == 0x12345678,
	       "invalid arguments reached mutation state");
	expect_unchanged_header(before, &fixture.skb);

	make_fixture(&fixture, 0x1234, 0x2222);
	fixture.skb.data_len = fixture.skb.len + 1;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "impossible skb lengths accepted");

	make_fixture(&fixture, 0x1234, 0x2222);
	fixture.skb.len = fixture.skb.network_offset + UDP_OFFSET + 7;
	fixture.skb.head_length = fixture.skb.len;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "truncated UDP header accepted");

	make_fixture(&fixture, 0x1234, 0x2222);
	fixture.skb.len = fixture.skb.network_offset + UDP_OFFSET + 15;
	fixture.skb.head_length = fixture.skb.len;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "datagram beyond skb length accepted");

	make_fixture(&fixture, 0x1234, 0x2222);
	fixture.skb.copy_error = true;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "unreadable UDP header accepted");

	make_fixture(&fixture, 0x1234, 0x2222);
	fixture.skb.data_len = 0;
	fixture.skb.network_offset = -1;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "negative network offset accepted");

#if SIZE_MAX > UINT_MAX
	make_fixture(&fixture, 0x1234, 0x2222);
	changed = view;
	changed.datagram_offset = (size_t)UINT_MAX - INT_MAX + 1;
	changed.payload_offset = changed.datagram_offset + 8;
	fixture.skb.network_offset = INT_MAX;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "network and datagram offsets wrapped unsigned int");
	EXPECT(fixture.skb.copy_calls == 0,
	       "overflowing offset reached skb_copy_bits");

	make_fixture(&fixture, 0x1234, 0x2222);
	changed = view;
	changed.datagram_offset = (size_t)INT_MAX + 1;
	changed.payload_offset = changed.datagram_offset + 8;
	fixture.skb.network_offset = 0;
	fixture.skb.len = UINT_MAX;
	fixture.skb.head_length = UINT_MAX;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &changed, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "offset outside skb_copy_bits int range accepted");
	EXPECT(fixture.skb.copy_calls == 0,
	       "out-of-range int conversion reached skb_copy_bits");
#endif
	expect_unchanged_header(before, &(struct sk_buff) {
		.data = fixture.storage + 1,
		.network_offset = 3,
	});
}

static void test_unsupported_states(void)
{
	struct fixture fixture;
	struct rk_udp_view view = make_view(0x1234);

	make_fixture(&fixture, 0x1234, 0x2222);
	fixture.skb.nfct = true;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	       "conntrack state accepted");
	EXPECT(!writable_calls && !checksum_calls &&
	       fixture.skb.hash == 0x12345678,
	       "conntrack rejection changed packet state");
	fixture.skb.nfct = false;
	fixture.skb.secpath = true;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	       "XFRM state accepted");
	EXPECT(!writable_calls && !checksum_calls &&
	       fixture.skb.hash == 0x12345678,
	       "XFRM rejection changed packet state");
	fixture.skb.secpath = false;
	fixture.skb.gso = true;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	       "GSO without partial checksum accepted");
	EXPECT(!writable_calls && !checksum_calls,
	       "incoherent GSO reached mutation helpers");
	fixture.skb.ip_summed = CHECKSUM_PARTIAL;
	fixture.skb.csum_start = fixture.skb.network_offset + UDP_OFFSET;
	fixture.skb.csum_offset = offsetof(struct udphdr, check);
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
	       "coherent UDP GSO was rejected");
	EXPECT(ntohs(fixture_udp(&fixture)->check) == 0x2222 &&
	       checksum_calls == 1,
	       "partial GSO seed was not preserved by the checksum helper");

	make_fixture(&fixture, 0x1234, 0x2222);
	fixture.skb.ip_summed = CHECKSUM_PARTIAL;
	fixture.skb.csum_start = fixture.skb.network_offset + UDP_OFFSET + 1;
	fixture.skb.csum_offset = offsetof(struct udphdr, check);
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	       "wrong partial start accepted");
	fixture.skb.csum_start--;
	fixture.skb.csum_offset++;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	       "wrong checksum offset accepted");
	fixture.skb.csum_offset--;
	fixture.skb.csum_not_inet = 1;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	       "CRC32c partial state accepted");
}

int main(void)
{
	test_checksum_space();
	test_checksum_modes();
	test_relocation_and_stale_views();
	test_invalid_inputs_and_geometry();
	test_unsupported_states();
	printf("[+] %lu bounded mutation assertions passed\n", assertions);
	return EXIT_SUCCESS;
}
