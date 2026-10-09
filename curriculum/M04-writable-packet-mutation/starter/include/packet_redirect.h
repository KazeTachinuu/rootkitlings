/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_PACKET_REDIRECT_H
#define ROOTKITLINGS_PACKET_REDIRECT_H

#include <linux/atomic.h>
#include <linux/compiler_attributes.h>
#include <linux/netfilter.h>
#include <linux/types.h>

#include "udp_classifier.h"
#include "udp_redirect.h"

struct net;
struct sk_buff;

typedef bool (*rk_packet_enabled_fn)(void *context);

enum rk_packet_result {
	RK_PACKET_MATCH,
	RK_PACKET_OTHER_PROTOCOL,
	RK_PACKET_OTHER_PORT,
	RK_PACKET_TRUNCATED,
	RK_PACKET_MALFORMED,
	RK_PACKET_ACCESS_ERROR,
	RK_PACKET_IPV4_REJECTED,
};

struct rk_netfilter_config {
	unsigned int match_port;
	unsigned int replacement_port;
	rk_packet_enabled_fn enabled;
	void *enabled_context;
	int registration_error;
};

struct rk_netfilter_mutator {
	struct nf_hook_ops ops;
	atomic64_t callbacks;
	atomic64_t rewritten;
	atomic64_t unchanged;
	atomic64_t invalid;
	atomic64_t unsupported;
	atomic64_t stale;
	atomic64_t not_writable;
	struct net *net;
	rk_packet_enabled_fn enabled;
	void *enabled_context;
	unsigned int match_port;
	unsigned int replacement_port;
	bool registered;
};

struct rk_netfilter_status {
	bool registered;
	unsigned int match_port;
	unsigned int replacement_port;
	u64 callbacks;
	u64 rewritten;
	u64 unchanged;
	u64 invalid;
	u64 unsupported;
	u64 stale;
	u64 not_writable;
};

enum rk_packet_result
rk_inspect_ipv4_udp(const struct sk_buff *skb, unsigned int target_port,
		    struct rk_udp_view *view);
__must_check int
rk_netfilter_start(struct rk_netfilter_mutator *mutator,
		   const struct rk_netfilter_config *config);
void rk_netfilter_stop(struct rk_netfilter_mutator *mutator);
void rk_netfilter_snapshot(const struct rk_netfilter_mutator *mutator,
			   struct rk_netfilter_status *status);

#endif
