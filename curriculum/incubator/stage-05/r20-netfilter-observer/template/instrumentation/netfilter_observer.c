// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/rcupdate.h>
#include <linux/string.h>
#include <net/net_namespace.h>

#include "rootkit.h"

static unsigned int __maybe_unused rk_observe_local_ipv4(void *private,
					  struct sk_buff *skb,
					  const struct nf_hook_state *state)
{
	(void)private;
	(void)skb;
	(void)state;
	/* Count this hook traversal, then allow it to continue. */
	return NF_ACCEPT;
}

__must_check int
rk_netfilter_start(struct rk_netfilter_observer *observer)
{
	/* Initialize and register one init_net IPv4 LOCAL_OUT observer. */
	(void)observer;
	return -EOPNOTSUPP;
}

void rk_netfilter_stop(struct rk_netfilter_observer *observer)
{
	/* Unregister, drain callback readers, then clear published state. */
	(void)observer;
}

void rk_netfilter_snapshot(const struct rk_netfilter_observer *observer,
			   struct rk_netfilter_status *status)
{
	/* Read one coherent field-by-field status snapshot. */
	(void)observer;
	memset(status, 0, sizeof(*status));
}
