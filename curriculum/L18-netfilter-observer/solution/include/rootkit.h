// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/atomic.h>
#include <linux/compiler_attributes.h>
#include <linux/netfilter.h>
#include <linux/types.h>

#define RK_FAIL_NONE 0
#define RK_FAIL_NETFILTER_REGISTER 1
#define RK_FAIL_PROC_PUBLISH 2

struct net;

struct rk_netfilter_observer {
	struct nf_hook_ops ops;
	atomic64_t callbacks;
	struct net *net;
	bool registered;
};

struct rk_netfilter_status {
	bool registered;
	u8 family;
	unsigned int hook;
	int priority;
	bool init_namespace;
	u64 callbacks;
};

__must_check int rk_netfilter_start(struct rk_netfilter_observer *observer);
void rk_netfilter_stop(struct rk_netfilter_observer *observer);
void rk_netfilter_snapshot(const struct rk_netfilter_observer *observer,
			   struct rk_netfilter_status *status);

__must_check int rk_proc_publish(struct rk_netfilter_observer *observer);
void rk_proc_unpublish(void);

#endif
