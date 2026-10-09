// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/refcount.h>
#include <linux/skbuff.h>
#include <linux/string.h>

#include "ipv4_parser.h"
#include "skb_access.h"

enum rk_inspect_result {
	RK_INSPECT_OK,
	RK_INSPECT_ACCESS_INVALID_ARGUMENT,
	RK_INSPECT_ACCESS_INVALID_GEOMETRY,
	RK_INSPECT_ACCESS_TRUNCATED,
	RK_INSPECT_ACCESS_UNREADABLE,
	RK_INSPECT_PARSE_TRUNCATED,
	RK_INSPECT_PARSE_MALFORMED,
	RK_INSPECT_PARSE_FRAGMENTED,
};

struct rk_skb_snapshot {
	unsigned int len;
	unsigned int data_len;
	sk_buff_data_t tail;
	__u16 network_header;
	unsigned int nr_frags;
	struct sk_buff *frag_list;
	unsigned char *data;
	int users;
	bool cloned;
	bool unreadable;
};

static unsigned int rk_assertions;

static int rk_expect(bool condition, const char *label)
{
	rk_assertions++;
	if (condition)
		return 0;
	pr_err("rootkitlings: self-test failed: %s\n", label);
	return -EINVAL;
}

static void rk_snapshot(const struct sk_buff *skb,
			struct rk_skb_snapshot *snapshot)
{
	snapshot->len = skb->len;
	snapshot->data_len = skb->data_len;
	snapshot->tail = skb->tail;
	snapshot->network_header = skb->network_header;
	snapshot->nr_frags = skb_shinfo(skb)->nr_frags;
	snapshot->frag_list = skb_shinfo(skb)->frag_list;
	snapshot->data = skb->data;
	snapshot->users = refcount_read(&skb->users);
	snapshot->cloned = skb->cloned;
	snapshot->unreadable = skb->unreadable;
}

static bool rk_unchanged(const struct sk_buff *skb,
			 const struct rk_skb_snapshot *snapshot)
{
	return skb->len == snapshot->len &&
	       skb->data_len == snapshot->data_len &&
	       skb->tail == snapshot->tail &&
	       skb->network_header == snapshot->network_header &&
	       skb_shinfo(skb)->nr_frags == snapshot->nr_frags &&
	       skb_shinfo(skb)->frag_list == snapshot->frag_list &&
	       skb->data == snapshot->data &&
	       refcount_read(&skb->users) == snapshot->users &&
	       skb->cloned == snapshot->cloned &&
	       skb->unreadable == snapshot->unreadable;
}

static void rk_make_ipv4(unsigned char *packet, size_t length,
			 unsigned int ihl, unsigned int total_length,
			 unsigned char protocol)
{
	memset(packet, 0, length);
	packet[0] = 0x40 | ihl;
	packet[2] = total_length >> 8;
	packet[3] = total_length;
	packet[8] = 64;
	packet[9] = protocol;
}

static struct sk_buff *rk_linear_skb(const unsigned char *packet,
				     unsigned int packet_length,
				     unsigned int before,
				     unsigned int after)
{
	struct sk_buff *skb;
	unsigned char *bytes;

	skb = alloc_skb(32 + before + packet_length + after, GFP_KERNEL);
	if (!skb)
		return NULL;
	skb_reserve(skb, 16);
	bytes = skb_put(skb, before + packet_length + after);
	memset(bytes, 0xa5, before + packet_length + after);
	memcpy(bytes + before, packet, packet_length);
	skb_set_network_header(skb, before);
	return skb;
}

static enum rk_inspect_result
rk_inspect_ipv4(const struct sk_buff *skb, struct rk_ipv4_view *view)
{
	unsigned char prefix[RK_IPV4_PREFIX_LENGTH];
	enum rk_skb_access_result access_result;
	enum rk_ipv4_parse_result parse_result;
	size_t available;

	access_result = rk_skb_read_ipv4_prefix(skb, prefix, &available);
	switch (access_result) {
	case RK_SKB_ACCESS_INVALID_ARGUMENT:
		return RK_INSPECT_ACCESS_INVALID_ARGUMENT;
	case RK_SKB_ACCESS_INVALID_GEOMETRY:
		return RK_INSPECT_ACCESS_INVALID_GEOMETRY;
	case RK_SKB_ACCESS_TRUNCATED:
		return RK_INSPECT_ACCESS_TRUNCATED;
	case RK_SKB_ACCESS_UNREADABLE:
		return RK_INSPECT_ACCESS_UNREADABLE;
	case RK_SKB_ACCESS_OK:
		break;
	}

	parse_result = rk_parse_ipv4(prefix, available, view);
	switch (parse_result) {
	case RK_IPV4_PARSE_TRUNCATED:
		return RK_INSPECT_PARSE_TRUNCATED;
	case RK_IPV4_PARSE_MALFORMED:
		return RK_INSPECT_PARSE_MALFORMED;
	case RK_IPV4_PARSE_FRAGMENTED:
		return RK_INSPECT_PARSE_FRAGMENTED;
	case RK_IPV4_PARSE_OK:
		return RK_INSPECT_OK;
	}
	return RK_INSPECT_PARSE_MALFORMED;
}

static int rk_test_arguments_and_geometry(void)
{
	unsigned char packet[20];
	unsigned char prefix[20];
	struct sk_buff *skb;
	size_t available;
	unsigned int saved;
	int ret = -EINVAL;

	rk_make_ipv4(packet, sizeof(packet), 5, 20, 17);
	skb = rk_linear_skb(packet, sizeof(packet), 0, 0);
	if (rk_expect(skb != NULL, "argument fixture allocation"))
		return -ENOMEM;
	memset(prefix, 0x5a, sizeof(prefix));
	available = 0x5a5a;
	if (rk_expect(rk_skb_read_ipv4_prefix(NULL, prefix, &available) ==
		      RK_SKB_ACCESS_INVALID_ARGUMENT,
		      "null skb") ||
	    rk_expect(available == 0x5a5a && prefix[0] == 0x5a,
		      "null skb leaves outputs") ||
	    rk_expect(rk_skb_read_ipv4_prefix(skb, NULL, &available) ==
		      RK_SKB_ACCESS_INVALID_ARGUMENT, "null prefix") ||
	    rk_expect(rk_skb_read_ipv4_prefix(skb, prefix, NULL) ==
		      RK_SKB_ACCESS_INVALID_ARGUMENT, "null available"))
		goto out;

	saved = skb->data_len;
	skb->data_len = skb->len + 1;
	if (rk_expect(rk_skb_read_ipv4_prefix(skb, prefix, &available) ==
		      RK_SKB_ACCESS_INVALID_GEOMETRY, "data_len exceeds len"))
		goto restore_data_len;
	skb->data_len = saved;

	saved = skb->len;
	skb->len = (unsigned int)INT_MAX + 1U;
	if (rk_expect(rk_skb_read_ipv4_prefix(skb, prefix, &available) ==
		      RK_SKB_ACCESS_INVALID_GEOMETRY, "len exceeds helper range"))
		goto restore_len;
	skb->len = saved;
	memset(prefix, 0x5a, sizeof(prefix));
	available = 0x5a5a;
	if (rk_expect(rk_skb_read_network(skb, 0, prefix, 0, &available) ==
		      RK_SKB_ACCESS_INVALID_ARGUMENT, "zero-length request") ||
	    rk_expect(rk_skb_read_network(skb, 0, prefix,
					  RK_SKB_READ_LIMIT + 1, &available) ==
		      RK_SKB_ACCESS_INVALID_ARGUMENT, "oversize request") ||
	    rk_expect(rk_skb_read_network(skb, (size_t)S64_MAX + 1,
					  prefix, 1, &available) ==
		      RK_SKB_ACCESS_INVALID_ARGUMENT, "offset exceeds s64") ||
	    rk_expect(available == 0x5a5a && prefix[0] == 0x5a,
		      "invalid requests leave outputs") ||
	    rk_expect(rk_skb_read_network(skb, skb->len + 1, prefix, 1,
					  &available) == RK_SKB_ACCESS_TRUNCATED,
		      "relative offset truncation") ||
	    rk_expect(rk_skb_read_network(skb, 0, prefix, skb->len + 1,
					  &available) == RK_SKB_ACCESS_TRUNCATED,
		      "relative length truncation") ||
	    rk_expect(available == 0x5a5a && prefix[0] == 0x5a,
		      "truncated requests leave outputs"))
		goto out;

	skb_set_network_header(skb, skb->len + 1);
	if (rk_expect(rk_skb_read_ipv4_prefix(skb, prefix, &available) ==
		      RK_SKB_ACCESS_INVALID_GEOMETRY,
		      "network header follows packet"))
		goto out;
	ret = 0;
	goto out;

restore_len:
	skb->len = saved;
	goto out;
restore_data_len:
	skb->data_len = saved;
out:
	kfree_skb(skb);
	return ret;
}

static int rk_test_linear_offsets(void)
{
	unsigned char packet[40];
	unsigned char prefix[20];
	struct rk_skb_snapshot snapshot;
	struct sk_buff *skb;
	size_t available = 0;
	int ret = -EINVAL;

	rk_make_ipv4(packet, sizeof(packet), 5, sizeof(packet), 17);
	skb = rk_linear_skb(packet, sizeof(packet), 8, 7);
	if (rk_expect(skb != NULL, "linear fixture allocation"))
		return -ENOMEM;
	rk_snapshot(skb, &snapshot);
	if (rk_expect(rk_skb_read_ipv4_prefix(skb, prefix, &available) ==
		      RK_SKB_ACCESS_OK, "positive network offset") ||
	    rk_expect(available == sizeof(packet) + 7, "positive available") ||
	    rk_expect(!memcmp(prefix, packet, sizeof(prefix)),
		      "positive prefix") ||
	    rk_expect(rk_unchanged(skb, &snapshot), "positive read-only"))
		goto out;
	memset(prefix, 0, sizeof(prefix));
	if (rk_expect(rk_skb_read_network(skb, 3, prefix, 8, &available) ==
		      RK_SKB_ACCESS_OK, "positive relative read") ||
	    rk_expect(!memcmp(prefix, packet + 3, 8),
		      "positive relative bytes") ||
	    rk_expect(available == sizeof(packet) + 7,
		      "relative read reports total available"))
		goto out;

	skb_pull(skb, 16);
	rk_snapshot(skb, &snapshot);
	memset(prefix, 0, sizeof(prefix));
	available = 0;
	if (rk_expect(skb_network_offset(skb) == -8,
		      "negative fixture offset") ||
	    rk_expect(rk_skb_read_ipv4_prefix(skb, prefix, &available) ==
		      RK_SKB_ACCESS_OK, "negative network offset") ||
	    rk_expect(available == sizeof(packet) + 7,
		      "negative available") ||
	    rk_expect(!memcmp(prefix, packet, sizeof(prefix)),
		      "negative prefix") ||
	    rk_expect(rk_unchanged(skb, &snapshot), "negative read-only"))
		goto out;
	memset(prefix, 0, sizeof(prefix));
	if (rk_expect(rk_skb_read_network(skb, 13, prefix, 6, &available) ==
		      RK_SKB_ACCESS_OK, "negative base with relative offset") ||
	    rk_expect(!memcmp(prefix, packet + 13, 6),
		      "negative relative bytes"))
		goto out;
	ret = 0;
out:
	kfree_skb(skb);
	return ret;
}

static int rk_test_truncation(void)
{
	unsigned char packet[20];
	unsigned int length;

	rk_make_ipv4(packet, sizeof(packet), 5, 20, 17);
	for (length = 0; length < sizeof(packet); length++) {
		unsigned char prefix[20];
		struct sk_buff *skb;
		size_t available = 0x31337;
		int failed;

		skb = rk_linear_skb(packet, length, 0, 0);
		if (rk_expect(skb != NULL, "truncation fixture allocation"))
			return -ENOMEM;
		memset(prefix, 0x3c, sizeof(prefix));
		failed = rk_expect(rk_skb_read_ipv4_prefix(skb, prefix,
							     &available) ==
				   RK_SKB_ACCESS_TRUNCATED, "short prefix") ||
			 rk_expect(available == 0x31337 && prefix[0] == 0x3c,
				   "truncation leaves outputs");
		kfree_skb(skb);
		if (failed)
			return -EINVAL;
	}
	return 0;
}

static int rk_test_page_fragments(void)
{
	unsigned char packet[20];
	unsigned int split;

	rk_make_ipv4(packet, sizeof(packet), 5, 20, 17);
	for (split = 0; split <= sizeof(packet); split++) {
		unsigned char prefix[20];
		struct rk_skb_snapshot snapshot;
		struct sk_buff *skb;
		struct page *page = NULL;
		void *mapping;
		size_t available = 0;
		int page_refs = 0;
		int failed;

		skb = alloc_skb(64, GFP_KERNEL);
		if (rk_expect(skb != NULL, "page-fragment skb allocation"))
			return -ENOMEM;
		memcpy(skb_put(skb, split), packet, split);
		if (split < sizeof(packet)) {
			page = alloc_page(GFP_KERNEL);
			if (rk_expect(page != NULL, "page-fragment allocation")) {
				kfree_skb(skb);
				return -ENOMEM;
			}
			mapping = kmap_local_page(page);
			memcpy(mapping, packet + split, sizeof(packet) - split);
			kunmap_local(mapping);
			skb_add_rx_frag(skb, 0, page, 0,
					sizeof(packet) - split, PAGE_SIZE);
			page_refs = page_ref_count(page);
		}
		skb_reset_network_header(skb);
		rk_snapshot(skb, &snapshot);
		failed = rk_expect(rk_skb_read_ipv4_prefix(skb, prefix,
							     &available) ==
				   RK_SKB_ACCESS_OK, "page-fragment prefix") ||
			 rk_expect(available == sizeof(packet),
				   "page-fragment available") ||
			 rk_expect(!memcmp(prefix, packet, sizeof(prefix)),
				   "page-fragment bytes") ||
			 rk_expect(rk_unchanged(skb, &snapshot),
				   "page-fragment read-only") ||
			 rk_expect(!page || page_ref_count(page) == page_refs,
				   "page-fragment reference count");
		if (!failed) {
			memset(prefix, 0, sizeof(prefix));
			failed = rk_expect(rk_skb_read_network(skb, 4, prefix, 12,
							       &available) ==
					   RK_SKB_ACCESS_OK,
					   "page-fragment relative read") ||
				 rk_expect(!memcmp(prefix, packet + 4, 12),
					   "page-fragment relative bytes");
		}
		kfree_skb(skb);
		if (failed)
			return -EINVAL;
	}
	return 0;
}

static int rk_test_frag_list(void)
{
	unsigned char packet[20];
	unsigned char prefix[20];
	struct rk_skb_snapshot snapshot;
	struct sk_buff *child;
	struct sk_buff *skb;
	size_t available = 0;
	int ret = -EINVAL;

	rk_make_ipv4(packet, sizeof(packet), 5, 20, 17);
	skb = alloc_skb(64, GFP_KERNEL);
	child = alloc_skb(64, GFP_KERNEL);
	if (rk_expect(skb && child, "frag_list allocation")) {
		kfree_skb(skb);
		kfree_skb(child);
		return -ENOMEM;
	}
	memcpy(skb_put(skb, 6), packet, 6);
	memcpy(skb_put(child, 14), packet + 6, 14);
	skb_shinfo(skb)->frag_list = child;
	skb->len += child->len;
	skb->data_len += child->len;
	skb->truesize += child->truesize;
	skb_reset_network_header(skb);
	rk_snapshot(skb, &snapshot);
	if (rk_expect(rk_skb_read_ipv4_prefix(skb, prefix, &available) ==
		      RK_SKB_ACCESS_OK, "frag_list prefix") ||
	    rk_expect(available == sizeof(packet), "frag_list available") ||
	    rk_expect(!memcmp(prefix, packet, sizeof(prefix)),
		      "frag_list bytes") ||
	    rk_expect(rk_unchanged(skb, &snapshot), "frag_list read-only"))
		goto out;
	ret = 0;
out:
	kfree_skb(skb);
	return ret;
}

static int rk_test_shared_and_unreadable(void)
{
	unsigned char packet[20];
	unsigned char prefix[20];
	struct rk_skb_snapshot snapshot;
	struct sk_buff *clone;
	struct sk_buff *skb;
	struct page *page;
	size_t available;
	int ret = -EINVAL;

	rk_make_ipv4(packet, sizeof(packet), 5, 20, 17);
	skb = rk_linear_skb(packet, sizeof(packet), 0, 0);
	if (rk_expect(skb != NULL, "shared fixture allocation"))
		return -ENOMEM;
	clone = skb_clone(skb, GFP_KERNEL);
	if (rk_expect(clone != NULL, "clone allocation"))
		goto out_skb;
	rk_snapshot(clone, &snapshot);
	if (rk_expect(rk_skb_read_ipv4_prefix(clone, prefix, &available) ==
		      RK_SKB_ACCESS_OK, "cloned buffer") ||
	    rk_expect(!memcmp(prefix, packet, sizeof(prefix)), "cloned bytes") ||
	    rk_expect(rk_unchanged(clone, &snapshot), "clone read-only"))
		goto out_clone;
	kfree_skb(clone);
	kfree_skb(skb);

	skb = rk_linear_skb(packet, sizeof(packet), 0, 0);
	if (rk_expect(skb != NULL, "unreadable fixture allocation"))
		return -ENOMEM;
	page = alloc_page(GFP_KERNEL);
	if (rk_expect(page != NULL, "unreadable suffix allocation"))
		goto out_skb;
	skb_add_rx_frag(skb, 0, page, 0, 1, PAGE_SIZE);
	skb->unreadable = 1;
	memset(prefix, 0x69, sizeof(prefix));
	available = 0x6969;
	rk_snapshot(skb, &snapshot);
	if (rk_expect(rk_skb_read_ipv4_prefix(skb, prefix, &available) ==
		      RK_SKB_ACCESS_UNREADABLE, "unreadable fragments") ||
	    rk_expect(prefix[0] == 0x69 && available == 0x6969,
		      "unreadable leaves outputs") ||
	    rk_expect(rk_unchanged(skb, &snapshot), "unreadable read-only"))
		goto out_skb;
	ret = 0;
	goto out_skb;

out_clone:
	kfree_skb(clone);
out_skb:
	kfree_skb(skb);
	return ret;
}

static int rk_test_parser_integration(void)
{
	unsigned char packet[60];
	struct rk_ipv4_view view;
	struct sk_buff *skb;
	int ret = -EINVAL;

	rk_make_ipv4(packet, sizeof(packet), 15, 60, 47);
	skb = rk_linear_skb(packet, sizeof(packet), 0, 0);
	if (rk_expect(skb != NULL, "integration fixture allocation"))
		return -ENOMEM;
	if (rk_expect(rk_inspect_ipv4(skb, &view) == RK_INSPECT_OK,
		      "IHL 60 inspection") ||
	    rk_expect(view.header_length == 60 && view.packet_length == 60 &&
		      view.payload_length == 0 && view.protocol == 47,
		      "IHL 60 view"))
		goto out;
	kfree_skb(skb);

	rk_make_ipv4(packet, sizeof(packet), 5, 40, 17);
	skb = rk_linear_skb(packet, 20, 0, 0);
	if (rk_expect(skb != NULL, "declared-length fixture allocation"))
		return -ENOMEM;
	if (rk_expect(rk_inspect_ipv4(skb, &view) == RK_INSPECT_PARSE_TRUNCATED,
		      "declared Total Length truncation"))
		goto out;
	packet[0] = 0x65;
	memcpy(skb->data, packet, 20);
	if (rk_expect(rk_inspect_ipv4(skb, &view) == RK_INSPECT_PARSE_MALFORMED,
		      "malformed result mapping"))
		goto out;
	packet[0] = 0x45;
	packet[2] = 0;
	packet[3] = 20;
	packet[6] = 0x20;
	memcpy(skb->data, packet, 20);
	if (rk_expect(rk_inspect_ipv4(skb, &view) == RK_INSPECT_PARSE_FRAGMENTED,
		      "fragment result mapping"))
		goto out;
	ret = 0;
out:
	kfree_skb(skb);
	return ret;
}

static int __init rootkitlings_init(void)
{
	int ret;

	ret = rk_test_arguments_and_geometry();
	if (!ret)
		ret = rk_test_linear_offsets();
	if (!ret)
		ret = rk_test_truncation();
	if (!ret)
		ret = rk_test_page_fragments();
	if (!ret)
		ret = rk_test_frag_list();
	if (!ret)
		ret = rk_test_shared_and_unreadable();
	if (!ret)
		ret = rk_test_parser_integration();
	if (ret)
		return ret;
	pr_info("rootkitlings: skb access self-tests passed: %u\n",
		rk_assertions);
	return 0;
}

static void __exit rootkitlings_exit(void)
{
}

module_init(rootkitlings_init);
module_exit(rootkitlings_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings safe sk_buff access exercise");
