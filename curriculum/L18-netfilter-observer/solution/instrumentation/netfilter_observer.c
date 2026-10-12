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
	struct rk_netfilter_observer *observer = private;

	(void)skb;
	(void)state;
	atomic64_inc(&observer->callbacks);
	return NF_ACCEPT;
}

__must_check int
rk_netfilter_start(struct rk_netfilter_observer *observer)
{
	int error;

	if (!observer)
		return -EINVAL;
	if (READ_ONCE(observer->registered))
		return -EBUSY;

	atomic64_set(&observer->callbacks, 0);
	observer->net = &init_net;
	observer->ops = (struct nf_hook_ops) {
		.hook = rk_observe_local_ipv4,
		.priv = observer,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_LOCAL_OUT,
		.priority = NF_IP_PRI_FIRST,
	};

	if (RKLINGS_FAIL_POINT == RK_FAIL_NETFILTER_REGISTER)
		error = -EAFNOSUPPORT;
	else
		error = nf_register_net_hook(observer->net, &observer->ops);
	if (error) {
		observer->net = NULL;
		memset(&observer->ops, 0, sizeof(observer->ops));
		return error;
	}

	WRITE_ONCE(observer->registered, true);
	return 0;
}

void rk_netfilter_stop(struct rk_netfilter_observer *observer)
{
	if (!observer || !READ_ONCE(observer->registered))
		return;

	nf_unregister_net_hook(observer->net, &observer->ops);
	synchronize_rcu();
	WRITE_ONCE(observer->registered, false);
	observer->net = NULL;
}

void rk_netfilter_snapshot(const struct rk_netfilter_observer *observer,
			   struct rk_netfilter_status *status)
{
	status->registered = READ_ONCE(observer->registered);
	status->family = READ_ONCE(observer->ops.pf);
	status->hook = READ_ONCE(observer->ops.hooknum);
	status->priority = READ_ONCE(observer->ops.priority);
	status->init_namespace = READ_ONCE(observer->net) == &init_net;
	status->callbacks = atomic64_read(&observer->callbacks);
}
