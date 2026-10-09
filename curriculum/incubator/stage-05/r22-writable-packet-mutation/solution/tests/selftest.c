// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/udp.h>

#include "rootkit.h"
#include "writable.h"

enum {
	RK_TEST_MATCH_PORT = 47021,
	RK_TEST_REPLACEMENT_PORT = 47022,
	RK_TEST_PACKET_LENGTH = 36,
};

static unsigned int assertions;

static int rk_expect(bool condition, const char *label)
{
	assertions++;
	if (condition)
		return 0;
	pr_err("rootkitlings: self-test failed: %s\n", label);
	return -EINVAL;
}

static void rk_make_packet(unsigned char *packet, __sum16 checksum)
{
	memset(packet, 0, RK_TEST_PACKET_LENGTH);
	packet[0] = 0x45;
	packet[2] = RK_TEST_PACKET_LENGTH >> 8;
	packet[3] = RK_TEST_PACKET_LENGTH;
	packet[8] = 64;
	packet[9] = 17;
	packet[12] = 127;
	packet[15] = 1;
	packet[16] = 127;
	packet[19] = 1;
	packet[20] = 0x12;
	packet[21] = 0x34;
	packet[22] = RK_TEST_MATCH_PORT >> 8;
	packet[23] = (unsigned char)RK_TEST_MATCH_PORT;
	packet[24] = 0;
	packet[25] = 16;
	memcpy(packet + 26, &checksum, sizeof(checksum));
	memcpy(packet + 28, "payload!", 8);
}

static struct sk_buff *rk_linear_skb(const unsigned char *packet)
{
	struct sk_buff *skb = alloc_skb(RK_TEST_PACKET_LENGTH + 64, GFP_KERNEL);

	if (!skb)
		return NULL;
	skb_reserve(skb, 32);
	memcpy(skb_put(skb, RK_TEST_PACKET_LENGTH), packet,
	       RK_TEST_PACKET_LENGTH);
	skb_reset_network_header(skb);
	return skb;
}

static struct sk_buff *rk_split_skb(const unsigned char *packet,
				    unsigned int split)
{
	struct sk_buff *skb;
	struct page *page;
	void *mapping;

	if (split > RK_TEST_PACKET_LENGTH)
		return NULL;
	skb = alloc_skb(split + 64, GFP_KERNEL);
	if (!skb)
		return NULL;
	skb_reserve(skb, 32);
	memcpy(skb_put(skb, split), packet, split);
	page = alloc_page(GFP_KERNEL);
	if (!page) {
		kfree_skb(skb);
		return NULL;
	}
	mapping = kmap_local_page(page);
	memcpy(mapping, packet + split, RK_TEST_PACKET_LENGTH - split);
	kunmap_local(mapping);
	skb_add_rx_frag(skb, 0, page, 0, RK_TEST_PACKET_LENGTH - split,
			PAGE_SIZE);
	skb_reset_network_header(skb);
	return skb;
}

static int rk_view(struct sk_buff *skb, struct rk_udp_view *view)
{
	return rk_inspect_ipv4_udp(skb, RK_TEST_MATCH_PORT, view) ==
	       RK_PACKET_MATCH ? 0 : -EINVAL;
}

static int rk_test_linear_and_checksum_modes(void)
{
	const unsigned int modes[] = {
		CHECKSUM_NONE, CHECKSUM_COMPLETE, CHECKSUM_UNNECESSARY,
		CHECKSUM_PARTIAL,
	};
	unsigned char packet[RK_TEST_PACKET_LENGTH];
	struct rk_udp_view view;
	struct udphdr *udp;
	struct sk_buff *skb;
	unsigned int index;
	int error;

	for (index = 0; index < ARRAY_SIZE(modes); index++) {
		rk_make_packet(packet, 0);
		skb = rk_linear_skb(packet);
		if (rk_expect(skb != NULL, "linear fixture allocation"))
			return -ENOMEM;
		skb->ip_summed = modes[index];
		if (modes[index] == CHECKSUM_PARTIAL) {
			skb->csum_start = skb_headroom(skb) + 20;
			skb->csum_offset = offsetof(struct udphdr, check);
		}
		error = rk_view(skb, &view);
		if (!error)
			error = rk_expect(rk_redirect_ipv4_udp(
				skb, &view, RK_TEST_MATCH_PORT,
				RK_TEST_REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
				"each checksum mode redirects");
		udp = (struct udphdr *)(skb->data + 20);
		if (!error)
			error = rk_expect(ntohs(udp->dest) ==
					  RK_TEST_REPLACEMENT_PORT && !udp->check,
					  "zero checksum stays zero");
		if (!error && modes[index] == CHECKSUM_COMPLETE)
			error = rk_expect(skb->ip_summed == CHECKSUM_NONE,
					  "zero checksum invalidates complete state");
		if (!error && modes[index] == CHECKSUM_PARTIAL)
			error = rk_expect(skb->ip_summed == CHECKSUM_PARTIAL,
					  "zero partial checksum is not synthesized");
		kfree_skb(skb);
		if (error)
			return error;
	}
	return 0;
}

static int rk_test_writable_layouts(void)
{
	unsigned char packet[RK_TEST_PACKET_LENGTH];
	unsigned char saved_payload[8];
	struct rk_udp_view view;
	struct sk_buff *clone;
	struct sk_buff *skb;
	struct udphdr *udp;
	int error = 0;

	rk_make_packet(packet, (__force __sum16)0x3412);
	memcpy(saved_payload, packet + 28, sizeof(saved_payload));
	skb = rk_split_skb(packet, 24);
	if (rk_expect(skb != NULL, "fragmented fixture allocation"))
		return -ENOMEM;
	if (rk_view(skb, &view) ||
	    rk_expect(rk_redirect_ipv4_udp(skb, &view, RK_TEST_MATCH_PORT,
		      RK_TEST_REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
		      "nonlinear UDP header becomes writable")) {
		error = -EINVAL;
		goto out_split;
	}
	udp = (struct udphdr *)(skb->data + 20);
	if (rk_expect(ntohs(udp->dest) == RK_TEST_REPLACEMENT_PORT,
		      "fragmented header is redirected") ||
	    rk_expect(udp->source == htons(0x1234) && ntohs(udp->len) == 16,
		      "unrelated UDP fields stay unchanged") ||
	    rk_expect(!skb_copy_bits(skb, 28, packet, sizeof(saved_payload)) &&
		      !memcmp(packet, saved_payload, sizeof(saved_payload)),
		      "payload stays unchanged")) {
		error = -EINVAL;
		goto out_split;
	}
out_split:
	kfree_skb(skb);
	if (error)
		return error;

	rk_make_packet(packet, (__force __sum16)0x3412);
	skb = rk_linear_skb(packet);
	if (rk_expect(skb != NULL, "clone source allocation"))
		return -ENOMEM;
	clone = skb_clone(skb, GFP_KERNEL);
	if (rk_expect(clone != NULL, "shared clone allocation")) {
		kfree_skb(skb);
		return -ENOMEM;
	}
	if (rk_view(clone, &view) ||
	    rk_expect(rk_redirect_ipv4_udp(clone, &view, RK_TEST_MATCH_PORT,
		      RK_TEST_REPLACEMENT_PORT) == RK_UDP_REDIRECT_OK,
		      "cloned header becomes private") ||
	    rk_expect(((struct udphdr *)(skb->data + 20))->dest ==
		      htons(RK_TEST_MATCH_PORT), "clone source stays unchanged"))
		error = -EINVAL;
	kfree_skb(clone);
	kfree_skb(skb);
	return error;
}

static int rk_test_rejections(void)
{
	unsigned char packet[RK_TEST_PACKET_LENGTH];
	unsigned char before[8];
	struct rk_udp_view view;
	struct sk_buff *skb;
	unsigned int saved_data_len;
	int error = 0;

	rk_make_packet(packet, 0);
	skb = rk_linear_skb(packet);
	if (rk_expect(skb != NULL, "rejection fixture allocation"))
		return -ENOMEM;
	if (rk_view(skb, &view)) {
		error = -EINVAL;
		goto out;
	}
	memcpy(before, skb->data + 20, sizeof(before));
	rk_skb_fail_next_writable();
	if (rk_expect(rk_redirect_ipv4_udp(skb, &view, RK_TEST_MATCH_PORT,
		      RK_TEST_REPLACEMENT_PORT) == RK_UDP_REDIRECT_NOT_WRITABLE,
		      "writable failure is reported") ||
	    rk_expect(!memcmp(before, skb->data + 20, sizeof(before)),
		      "writable failure does not mutate header bytes")) {
		error = -EINVAL;
		goto out;
	}

	skb->ip_summed = CHECKSUM_PARTIAL;
	skb->csum_start = skb_headroom(skb) + 21;
	skb->csum_offset = offsetof(struct udphdr, check);
	if (rk_expect(rk_redirect_ipv4_udp(skb, &view, RK_TEST_MATCH_PORT,
		      RK_TEST_REPLACEMENT_PORT) ==
		      RK_UDP_REDIRECT_UNSUPPORTED_STATE,
		      "incoherent partial checksum is rejected")) {
		error = -EINVAL;
		goto out;
	}
	skb->ip_summed = CHECKSUM_NONE;
	skb_shinfo(skb)->gso_size = 8;
	if (rk_expect(rk_redirect_ipv4_udp(skb, &view, RK_TEST_MATCH_PORT,
		      RK_TEST_REPLACEMENT_PORT) ==
		      RK_UDP_REDIRECT_UNSUPPORTED_STATE,
		      "GSO without partial checksum is rejected")) {
		error = -EINVAL;
		goto out;
	}
	skb_shinfo(skb)->gso_size = 0;
	((struct udphdr *)(skb->data + 20))->dest = htons(47020);
	if (rk_expect(rk_redirect_ipv4_udp(skb, &view, RK_TEST_MATCH_PORT,
		      RK_TEST_REPLACEMENT_PORT) == RK_UDP_REDIRECT_STALE_VIEW,
		      "changed UDP header makes the view stale")) {
		error = -EINVAL;
		goto out;
	}
	((struct udphdr *)(skb->data + 20))->dest = htons(RK_TEST_MATCH_PORT);
	saved_data_len = skb->data_len;
	skb->data_len = skb->len + 1;
	if (rk_expect(rk_redirect_ipv4_udp(skb, &view, RK_TEST_MATCH_PORT,
		      RK_TEST_REPLACEMENT_PORT) ==
		      RK_UDP_REDIRECT_INVALID_GEOMETRY,
		      "inconsistent skb lengths are rejected"))
		error = -EINVAL;
	skb->data_len = saved_data_len;
out:
	kfree_skb(skb);
	return error;
}

__must_check int rk_run_selftests(void)
{
	int error;

	assertions = 0;
	error = rk_test_linear_and_checksum_modes();
	if (!error)
		error = rk_test_writable_layouts();
	if (!error)
		error = rk_test_rejections();
	if (error)
		return error;
	pr_info("rootkitlings: writable UDP mutation self-tests passed: %u assertions\n",
		assertions);
	return 0;
}
