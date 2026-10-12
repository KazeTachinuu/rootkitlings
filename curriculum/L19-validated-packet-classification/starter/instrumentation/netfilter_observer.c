// SPDX-License-Identifier: GPL-2.0
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/rcupdate.h>
#include <linux/string.h>
#include <net/net_namespace.h>

#include "ipv4_parser.h"
#include "rootkit.h"
#include "skb_access.h"
#include "udp_classifier.h"

enum rk_packet_result
rk_inspect_ipv4_udp(const struct sk_buff *skb, unsigned int target_port,
		    struct rk_udp_view *view)
{
	unsigned char ipv4_prefix[RK_IPV4_PREFIX_LENGTH];
	unsigned char udp_header[8];
	enum rk_udp_classify_result classify_result;
	enum rk_skb_access_result access_result;
	struct rk_ipv4_view ipv4;
	size_t available;

	access_result = rk_skb_read_ipv4_prefix(skb, ipv4_prefix, &available);
	if (access_result != RK_SKB_ACCESS_OK)
		return RK_PACKET_ACCESS_ERROR;
	if (rk_parse_ipv4(ipv4_prefix, available, &ipv4) != RK_IPV4_PARSE_OK)
		return RK_PACKET_IPV4_REJECTED;

	if (ipv4.protocol == 17 && ipv4.payload_length >= sizeof(udp_header)) {
		access_result = rk_skb_read_network(skb, ipv4.payload_offset,
					    udp_header, sizeof(udp_header),
					    &available);
		if (access_result != RK_SKB_ACCESS_OK)
			return RK_PACKET_ACCESS_ERROR;
		classify_result = rk_classify_ipv4_udp(&ipv4, udp_header,
					       sizeof(udp_header), target_port,
					       view);
	} else {
		classify_result = rk_classify_ipv4_udp(&ipv4, NULL, 0,
					       target_port, view);
	}

	switch (classify_result) {
	case RK_UDP_MATCH:
		return RK_PACKET_MATCH;
	case RK_UDP_OTHER_PROTOCOL:
		return RK_PACKET_OTHER_PROTOCOL;
	case RK_UDP_OTHER_PORT:
		return RK_PACKET_OTHER_PORT;
	case RK_UDP_TRUNCATED:
		return RK_PACKET_TRUNCATED;
	case RK_UDP_MALFORMED:
		return RK_PACKET_MALFORMED;
	}
	return RK_PACKET_MALFORMED;
}

static unsigned int __maybe_unused rk_observe_local_ipv4(void *private,
						  struct sk_buff *skb,
						  const struct nf_hook_state *state)
{
	struct rk_netfilter_observer *observer = private;
	struct rk_udp_view view;

	(void)state;
	atomic64_inc(&observer->callbacks);
	switch (rk_inspect_ipv4_udp(skb, observer->target_port, &view)) {
	case RK_PACKET_MATCH:
		atomic64_inc(&observer->match);
		if (view.trailing_length)
			atomic64_inc(&observer->trailing);
		break;
	case RK_PACKET_OTHER_PROTOCOL:
		atomic64_inc(&observer->other_protocol);
		break;
	case RK_PACKET_OTHER_PORT:
		atomic64_inc(&observer->other_port);
		break;
	case RK_PACKET_TRUNCATED:
		atomic64_inc(&observer->truncated);
		break;
	case RK_PACKET_MALFORMED:
		atomic64_inc(&observer->malformed);
		break;
	case RK_PACKET_ACCESS_ERROR:
		atomic64_inc(&observer->access_error);
		break;
	case RK_PACKET_IPV4_REJECTED:
		atomic64_inc(&observer->ipv4_rejected);
		break;
	}
	return NF_ACCEPT;
}

__must_check int
rk_netfilter_start(struct rk_netfilter_observer *observer,
		   unsigned int target_port)
{
	int error;

	if (!observer || !target_port || target_port > 65535)
		return -EINVAL;
	if (READ_ONCE(observer->registered))
		return -EBUSY;

	atomic64_set(&observer->callbacks, 0);
	atomic64_set(&observer->match, 0);
	atomic64_set(&observer->other_protocol, 0);
	atomic64_set(&observer->other_port, 0);
	atomic64_set(&observer->truncated, 0);
	atomic64_set(&observer->malformed, 0);
	atomic64_set(&observer->access_error, 0);
	atomic64_set(&observer->ipv4_rejected, 0);
	atomic64_set(&observer->trailing, 0);
	observer->target_port = target_port;
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
	status->target_port = READ_ONCE(observer->target_port);
	status->callbacks = atomic64_read(&observer->callbacks);
	status->match = atomic64_read(&observer->match);
	status->other_protocol = atomic64_read(&observer->other_protocol);
	status->other_port = atomic64_read(&observer->other_port);
	status->truncated = atomic64_read(&observer->truncated);
	status->malformed = atomic64_read(&observer->malformed);
	status->access_error = atomic64_read(&observer->access_error);
	status->ipv4_rejected = atomic64_read(&observer->ipv4_rejected);
	status->trailing = atomic64_read(&observer->trailing);
}
