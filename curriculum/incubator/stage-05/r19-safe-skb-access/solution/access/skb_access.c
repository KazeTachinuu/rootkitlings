// SPDX-License-Identifier: GPL-2.0
#include <linux/limits.h>
#include <linux/string.h>

#include "skb_access.h"

enum rk_skb_access_result
rk_skb_read_network(const struct sk_buff *skb, size_t offset,
		    unsigned char *bytes, size_t length, size_t *available)
{
	unsigned char local[RK_SKB_READ_LIMIT];
	const unsigned char *source;
	size_t logical_available;
	unsigned int distance;
	unsigned int headroom;
	int network_offset;
	s64 copy_offset;

	if (!skb || !bytes || !available || !length || offset > S64_MAX ||
	    length > RK_SKB_READ_LIMIT)
		return RK_SKB_ACCESS_INVALID_ARGUMENT;
	if (skb->data_len > skb->len || skb->len > INT_MAX)
		return RK_SKB_ACCESS_INVALID_GEOMETRY;

	headroom = skb_headroom(skb);
	if (headroom > INT_MAX)
		return RK_SKB_ACCESS_INVALID_GEOMETRY;
	network_offset = skb_network_offset(skb);
	if (network_offset == INT_MIN)
		return RK_SKB_ACCESS_INVALID_GEOMETRY;

	if (network_offset < 0) {
		distance = -network_offset;
		if (distance > headroom)
			return RK_SKB_ACCESS_INVALID_GEOMETRY;
		logical_available = (size_t)skb->len + distance;
	} else {
		distance = network_offset;
		if (distance > skb->len)
			return RK_SKB_ACCESS_INVALID_GEOMETRY;
		logical_available = skb->len - distance;
	}

	if (!skb_frags_readable(skb))
		return RK_SKB_ACCESS_UNREADABLE;
	if (offset > logical_available || length > logical_available - offset)
		return RK_SKB_ACCESS_TRUNCATED;

	if (network_offset > 0 && offset > S64_MAX - network_offset)
		return RK_SKB_ACCESS_INVALID_GEOMETRY;
	copy_offset = (s64)network_offset + (s64)offset;
	if (copy_offset < INT_MIN || copy_offset > INT_MAX)
		return RK_SKB_ACCESS_INVALID_GEOMETRY;
	source = skb_header_pointer_careful(skb, (int)copy_offset, (int)length,
					    local);
	if (!source)
		return RK_SKB_ACCESS_UNREADABLE;
	if (source != local)
		memcpy(local, source, length);
	memcpy(bytes, local, length);
	*available = logical_available;
	return RK_SKB_ACCESS_OK;
}
