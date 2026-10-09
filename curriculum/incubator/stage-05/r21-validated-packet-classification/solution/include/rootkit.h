// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/atomic.h>
#include <linux/compiler_attributes.h>
#include <linux/netfilter.h>
#include <linux/types.h>

#include "udp_classifier.h"

#define RK_FAIL_NONE 0
#define RK_FAIL_NETFILTER_REGISTER 1
#define RK_FAIL_PROC_PUBLISH 2

struct net;
struct sk_buff;

enum rk_packet_result {
	RK_PACKET_MATCH,
	RK_PACKET_OTHER_PROTOCOL,
	RK_PACKET_OTHER_PORT,
	RK_PACKET_TRUNCATED,
	RK_PACKET_MALFORMED,
	RK_PACKET_ACCESS_ERROR,
	RK_PACKET_IPV4_REJECTED,
};

struct rk_netfilter_observer {
	struct nf_hook_ops ops;
	atomic64_t callbacks;
	atomic64_t match;
	atomic64_t other_protocol;
	atomic64_t other_port;
	atomic64_t truncated;
	atomic64_t malformed;
	atomic64_t access_error;
	atomic64_t ipv4_rejected;
	atomic64_t trailing;
	struct net *net;
	unsigned int target_port;
	bool registered;
};

struct rk_netfilter_status {
	bool registered;
	unsigned int target_port;
	u64 callbacks;
	u64 match;
	u64 other_protocol;
	u64 other_port;
	u64 truncated;
	u64 malformed;
	u64 access_error;
	u64 ipv4_rejected;
	u64 trailing;
};

enum rk_packet_result
rk_inspect_ipv4_udp(const struct sk_buff *skb, unsigned int target_port,
		    struct rk_udp_view *view);
__must_check int rk_netfilter_start(struct rk_netfilter_observer *observer,
				    unsigned int target_port);
void rk_netfilter_stop(struct rk_netfilter_observer *observer);
void rk_netfilter_snapshot(const struct rk_netfilter_observer *observer,
			   struct rk_netfilter_status *status);

__must_check int rk_proc_publish(struct rk_netfilter_observer *observer);
void rk_proc_unpublish(void);

__must_check int rk_run_selftests(void);

#endif
