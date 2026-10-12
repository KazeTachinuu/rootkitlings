/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_SKB_ACCESS_H
#define RKLINGS_SKB_ACCESS_H

#include <linux/skbuff.h>
#include <linux/stddef.h>

#define RK_IPV4_PREFIX_LENGTH 20
#define RK_SKB_READ_LIMIT 64

enum rk_skb_access_result {
	RK_SKB_ACCESS_OK,
	RK_SKB_ACCESS_INVALID_ARGUMENT,
	RK_SKB_ACCESS_INVALID_GEOMETRY,
	RK_SKB_ACCESS_TRUNCATED,
	RK_SKB_ACCESS_UNREADABLE,
};

enum rk_skb_access_result
rk_skb_read_network(const struct sk_buff *skb, size_t offset,
		    unsigned char *bytes, size_t length, size_t *available);

static inline enum rk_skb_access_result
rk_skb_read_ipv4_prefix(const struct sk_buff *skb,
			unsigned char prefix[RK_IPV4_PREFIX_LENGTH],
			size_t *available)
{
	return rk_skb_read_network(skb, 0, prefix, RK_IPV4_PREFIX_LENGTH,
				   available);
}

#endif
