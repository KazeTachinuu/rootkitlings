// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/skbuff.h>

#include "writable.h"

static atomic_t fail_next = ATOMIC_INIT(0);

int rk_skb_make_writable(struct sk_buff *skb, unsigned int write_length)
{
	if (atomic_xchg(&fail_next, 0))
		return -ENOMEM;
	return skb_ensure_writable(skb, write_length);
}

void rk_skb_fail_next_writable(void)
{
	atomic_set(&fail_next, 1);
}
