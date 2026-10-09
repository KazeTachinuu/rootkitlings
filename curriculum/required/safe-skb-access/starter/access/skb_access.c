// SPDX-License-Identifier: GPL-2.0
#include "skb_access.h"

enum rk_skb_access_result
rk_skb_read_network(const struct sk_buff *skb, size_t offset,
		    unsigned char *bytes, size_t length, size_t *available)
{
	/* TODO: validate the geometry and copy through the safe helper. */
	return RK_SKB_ACCESS_INVALID_ARGUMENT;
}
