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
	(void)skb;
	checksum_calls++;
	checksum_from = from;
	checksum_to = to;
	checksum_pseudohdr = pseudohdr;
	if (*sum == htons(0x1111))
		*sum = 0;
	else
		*sum ^= from ^ to ^ htons(0x5a5a);
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
			if (checksum == 0x1111)
				EXPECT(udp->check == CSUM_MANGLED_0,
				       "computed zero was not mangled");
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

	fixture.skb.data_len = fixture.skb.len + 1;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "impossible skb lengths accepted");
	fixture.skb.data_len = 0;
	fixture.skb.network_offset = -1;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_INVALID_GEOMETRY,
	       "negative network offset accepted");
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
	fixture.skb.nfct = false;
	fixture.skb.secpath = true;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	       "XFRM state accepted");
	fixture.skb.secpath = false;
	fixture.skb.gso = true;
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_UNSUPPORTED_STATE,
	       "GSO without partial checksum accepted");
	fixture.skb.ip_summed = CHECKSUM_PARTIAL;
	fixture.skb.csum_start = fixture.skb.network_offset + UDP_OFFSET;
	fixture.skb.csum_offset = offsetof(struct udphdr, check);
	EXPECT(rk_redirect_ipv4_udp(&fixture.skb, &view, MATCH_PORT,
	       REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
	       "coherent UDP GSO was rejected");

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
