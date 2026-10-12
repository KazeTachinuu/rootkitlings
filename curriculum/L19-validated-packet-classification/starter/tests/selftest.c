// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/skbuff.h>
#include <linux/string.h>

#include "rootkit.h"

static unsigned int assertions;

static int rk_expect(bool condition, const char *label)
{
	assertions++;
	if (condition)
		return 0;
	pr_err("rklings: self-test failed: %s\n", label);
	return -EINVAL;
}

static void rk_make_packet(unsigned char *packet, size_t length,
			   unsigned int ihl, unsigned int total_length,
			   unsigned int destination_port,
			   unsigned int udp_length)
{
	unsigned int udp_offset = ihl * 4;

	memset(packet, 0, length);
	packet[0] = 0x40 | ihl;
	packet[2] = total_length >> 8;
	packet[3] = total_length;
	packet[8] = 64;
	packet[9] = 17;
	if (udp_offset + 8 > length)
		return;
	packet[udp_offset] = 0x12;
	packet[udp_offset + 1] = 0x34;
	packet[udp_offset + 2] = destination_port >> 8;
	packet[udp_offset + 3] = destination_port;
	packet[udp_offset + 4] = udp_length >> 8;
	packet[udp_offset + 5] = udp_length;
}

static struct sk_buff *rk_linear_skb(const unsigned char *packet,
				     unsigned int length)
{
	struct sk_buff *skb;

	skb = alloc_skb(length + 32, GFP_KERNEL);
	if (!skb)
		return NULL;
	memcpy(skb_put(skb, length), packet, length);
	skb_reset_network_header(skb);
	return skb;
}

static struct sk_buff *rk_split_skb(const unsigned char *packet,
				    unsigned int length,
				    unsigned int split)
{
	struct sk_buff *skb;
	struct page *page;
	void *mapping;

	if (split > length)
		return NULL;
	skb = alloc_skb(split + 32, GFP_KERNEL);
	if (!skb)
		return NULL;
	memcpy(skb_put(skb, split), packet, split);
	page = alloc_page(GFP_KERNEL);
	if (!page) {
		kfree_skb(skb);
		return NULL;
	}
	mapping = kmap_local_page(page);
	memcpy(mapping, packet + split, length - split);
	kunmap_local(mapping);
	skb_add_rx_frag(skb, 0, page, 0, length - split, PAGE_SIZE);
	skb_reset_network_header(skb);
	return skb;
}

static int rk_test_composed_match(void)
{
	unsigned char packet[32];
	struct rk_udp_view view = { 0 };
	struct sk_buff *skb;
	int result = -EINVAL;

	rk_make_packet(packet, sizeof(packet), 5, sizeof(packet), 47021, 12);
	skb = rk_split_skb(packet, sizeof(packet), 24);
	if (rk_expect(skb != NULL, "split fixture allocation"))
		return -ENOMEM;
	if (!rk_expect(rk_inspect_ipv4_udp(skb, 47021, &view) ==
		       RK_PACKET_MATCH, "split UDP header matches") &&
	    !rk_expect(view.datagram_offset == 20,
		       "datagram offset follows IPv4 header") &&
	    !rk_expect(view.datagram_length == 12,
		       "UDP length bounds the datagram") &&
	    !rk_expect(view.payload_offset == 28 && view.payload_length == 4,
		       "payload follows the UDP header") &&
	    !rk_expect(view.trailing_length == 0,
		       "complete datagram has no trailing bytes") &&
	    !rk_expect(view.source_port == 0x1234 &&
		       view.destination_port == 47021,
		       "ports use network byte order"))
		result = 0;
	kfree_skb(skb);
	return result;
}

static int rk_test_upstream_rejections(void)
{
	unsigned char packet[32];
	struct rk_udp_view view;
	struct sk_buff *skb;
	unsigned int saved_data_len;
	int result = -EINVAL;

	rk_make_packet(packet, sizeof(packet), 5, 40, 47021, 12);
	skb = rk_linear_skb(packet, sizeof(packet));
	if (rk_expect(skb != NULL, "total-length fixture allocation"))
		return -ENOMEM;
	if (rk_expect(rk_inspect_ipv4_udp(skb, 47021, &view) ==
		      RK_PACKET_IPV4_REJECTED,
		      "declared IPv4 length beyond storage is rejected"))
		goto out;

	rk_make_packet(packet, sizeof(packet), 15, sizeof(packet), 47021, 12);
	memcpy(skb->data, packet, sizeof(packet));
	if (rk_expect(rk_inspect_ipv4_udp(skb, 47021, &view) ==
		      RK_PACKET_IPV4_REJECTED,
		      "IHL beyond storage is rejected"))
		goto out;

	rk_make_packet(packet, sizeof(packet), 5, sizeof(packet), 47021, 12);
	memcpy(skb->data, packet, sizeof(packet));
	saved_data_len = skb->data_len;
	skb->data_len = skb->len + 1;
	if (rk_expect(rk_inspect_ipv4_udp(skb, 47021, &view) ==
		      RK_PACKET_ACCESS_ERROR,
		      "inconsistent skb geometry is rejected")) {
		skb->data_len = saved_data_len;
		goto out;
	}
	skb->data_len = saved_data_len;
	skb->unreadable = 1;
	if (rk_expect(rk_inspect_ipv4_udp(skb, 47021, &view) ==
		      RK_PACKET_ACCESS_ERROR,
		      "unreadable fragments are rejected"))
		goto out;
	skb->unreadable = 0;
	result = 0;
out:
	kfree_skb(skb);
	return result;
}

__must_check int rk_run_selftests(void)
{
	int error;

	assertions = 0;
	error = rk_test_composed_match();
	if (error)
		return error;
	error = rk_test_upstream_rejections();
	if (error)
		return error;
	pr_info("rklings: packet classifier self-tests passed: %u assertions\n",
		assertions);
	return 0;
}
