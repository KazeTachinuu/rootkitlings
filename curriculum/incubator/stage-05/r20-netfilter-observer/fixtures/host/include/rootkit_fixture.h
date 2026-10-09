// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_NETFILTER_FIXTURE_H
#define ROOTKITLINGS_NETFILTER_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define __maybe_unused
#define __must_check
#define READ_ONCE(value) (value)
#define WRITE_ONCE(target, value) ((target) = (value))

typedef uint8_t u8;
typedef uint64_t u64;

typedef struct {
	long long counter;
} atomic64_t;

static inline void atomic64_set(atomic64_t *value, long long count)
{
	value->counter = count;
}

static inline void atomic64_inc(atomic64_t *value)
{
	value->counter++;
}

static inline void atomic64_add(long long count, atomic64_t *value)
{
	value->counter += count;
}

static inline long long atomic64_inc_return(atomic64_t *value)
{
	return ++value->counter;
}

static inline long long atomic64_add_return(long long count, atomic64_t *value)
{
	value->counter += count;
	return value->counter;
}

static inline long long atomic64_fetch_add(long long count, atomic64_t *value)
{
	long long previous = value->counter;

	value->counter += count;
	return previous;
}

static inline long long atomic64_read(const atomic64_t *value)
{
	return value->counter;
}

struct net {
	unsigned int identity;
};

extern struct net init_net;

struct sk_buff {
	unsigned int len;
};

struct nf_hook_state {
	struct net *net;
};

typedef unsigned int (*nf_hookfn)(void *private, struct sk_buff *skb,
				 const struct nf_hook_state *state);

struct nf_hook_ops {
	nf_hookfn hook;
	void *priv;
	u8 pf;
	unsigned int hooknum;
	int priority;
};

enum {
	NF_DROP = 0,
	NF_ACCEPT = 1,
	NFPROTO_IPV4 = 2,
	NF_INET_PRE_ROUTING = 0,
	NF_INET_LOCAL_OUT = 3,
	NF_IP_PRI_FIRST = INT32_MIN,
};

int nf_register_net_hook(struct net *net, const struct nf_hook_ops *ops);
void nf_unregister_net_hook(struct net *net, const struct nf_hook_ops *ops);
void synchronize_rcu(void);

#define RK_FAIL_NONE 0
#define RK_FAIL_NETFILTER_REGISTER 1
#define RK_FAIL_PROC_PUBLISH 2

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

#endif
