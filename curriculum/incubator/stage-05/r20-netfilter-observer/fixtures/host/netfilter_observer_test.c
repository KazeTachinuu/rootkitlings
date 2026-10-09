// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "rootkit_fixture.h"

enum event {
	EVENT_REGISTER = 1,
	EVENT_UNREGISTER,
	EVENT_SYNCHRONIZE,
};

struct net init_net = { .identity = 1 };
static struct net __attribute__((unused)) other_net = { .identity = 2 };
static struct rk_netfilter_observer *observed;
static const struct nf_hook_ops *registered_ops;
static int register_result;
static enum event events[8];
static size_t event_count;
static unsigned int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static void record(enum event event)
{
	CHECK(event_count < sizeof(events) / sizeof(events[0]));
	if (event_count < sizeof(events) / sizeof(events[0]))
		events[event_count++] = event;
}

int nf_register_net_hook(struct net *net, const struct nf_hook_ops *ops)
{
	record(EVENT_REGISTER);
	CHECK(observed != NULL);
	CHECK(net == &init_net);
	CHECK(observed->net == &init_net);
	CHECK(!observed->registered);
	CHECK(ops == &observed->ops);
	CHECK(ops->hook != NULL);
	CHECK(ops->priv == observed);
	CHECK(ops->pf == NFPROTO_IPV4);
	CHECK(ops->hooknum == NF_INET_LOCAL_OUT);
	CHECK(ops->priority == NF_IP_PRI_FIRST);
	if (!register_result)
		registered_ops = ops;
	return register_result;
}

void nf_unregister_net_hook(struct net *net, const struct nf_hook_ops *ops)
{
	record(EVENT_UNREGISTER);
	CHECK(observed != NULL);
	CHECK(observed->registered);
	CHECK(observed->net == &init_net);
	CHECK(net == &init_net);
	CHECK(ops == registered_ops);
}

void synchronize_rcu(void)
{
	record(EVENT_SYNCHRONIZE);
	CHECK(observed != NULL);
	CHECK(observed->registered);
	CHECK(observed->net == &init_net);
	CHECK(event_count >= 2 && events[event_count - 2] == EVENT_UNREGISTER);
}

static void reset_fixture(struct rk_netfilter_observer *observer)
{
	memset(observer, 0xa5, sizeof(*observer));
	observer->registered = false;
	observer->net = NULL;
	observed = observer;
	registered_ops = NULL;
	register_result = 0;
	event_count = 0;
}

static void __attribute__((unused))
expect_events(const enum event *expected, size_t count)
{
	size_t index;

	CHECK(event_count == count);
	for (index = 0; index < count && index < event_count; index++)
		CHECK(events[index] == expected[index]);
}

#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_NONE
static void check_start_and_callback(void)
{
	static const enum event expected[] = { EVENT_REGISTER };
	struct rk_netfilter_observer observer;
	struct nf_hook_state state = { .net = &other_net };
	struct sk_buff packet = { .len = 0 };
	u64 before;
	int index;

	reset_fixture(&observer);
	CHECK(rk_netfilter_start(NULL) == -EINVAL);
	CHECK(event_count == 0);
	CHECK(rk_netfilter_start(&observer) == 0);
	expect_events(expected, sizeof(expected) / sizeof(expected[0]));
	CHECK(observer.registered);
	CHECK(observer.net == &init_net);
	CHECK(atomic64_read(&observer.callbacks) == 0);
	CHECK(registered_ops == &observer.ops);

	for (index = 0; index < 3; index++) {
		before = (u64)atomic64_read(&observer.callbacks);
		packet.len = (unsigned int)(index * 97);
		state.net = index == 1 ? &init_net : &other_net;
		CHECK(registered_ops->hook(registered_ops->priv, &packet, &state) ==
		      NF_ACCEPT);
		CHECK((u64)atomic64_read(&observer.callbacks) == before + 1);
	}
	before = (u64)atomic64_read(&observer.callbacks);
	CHECK(registered_ops->hook(registered_ops->priv,
				   (struct sk_buff *)(uintptr_t)1,
				   (const struct nf_hook_state *)(uintptr_t)1) ==
	      NF_ACCEPT);
	CHECK((u64)atomic64_read(&observer.callbacks) == before + 1);
	CHECK(rk_netfilter_start(&observer) == -EBUSY);
	CHECK(event_count == 1);
}

static void check_registration_error(void)
{
	static const enum event expected[] = { EVENT_REGISTER };
	struct rk_netfilter_observer observer;
	int result;

	reset_fixture(&observer);
	register_result = -EADDRINUSE;
	result = rk_netfilter_start(&observer);
	CHECK(result == -EADDRINUSE);
	expect_events(expected, sizeof(expected) / sizeof(expected[0]));
	CHECK(!observer.registered);
	CHECK(observer.net == NULL);
	CHECK(memcmp(&observer.ops, &(struct nf_hook_ops) { 0 },
		     sizeof(observer.ops)) == 0);
	CHECK(registered_ops == NULL);
}

static void check_stop(void)
{
	static const enum event expected[] = {
		EVENT_REGISTER, EVENT_UNREGISTER, EVENT_SYNCHRONIZE,
	};
	struct rk_netfilter_observer observer;

	reset_fixture(&observer);
	rk_netfilter_stop(NULL);
	CHECK(event_count == 0);
	rk_netfilter_stop(&observer);
	CHECK(event_count == 0);
	CHECK(rk_netfilter_start(&observer) == 0);
	rk_netfilter_stop(&observer);
	expect_events(expected, sizeof(expected) / sizeof(expected[0]));
	CHECK(!observer.registered);
	CHECK(observer.net == NULL);
	rk_netfilter_stop(&observer);
	CHECK(event_count == 3);
}

static void check_snapshot(void)
{
	struct rk_netfilter_observer observer;
	struct rk_netfilter_status status;

	memset(&observer, 0, sizeof(observer));
	observer.registered = true;
	observer.ops.pf = 91;
	observer.ops.hooknum = 0x12345678U;
	observer.ops.priority = 0x234567;
	observer.net = &other_net;
	atomic64_set(&observer.callbacks, 0x123456789LL);
	memset(&status, 0, sizeof(status));
	rk_netfilter_snapshot(&observer, &status);
	CHECK(status.registered);
	CHECK(status.family == 91);
	CHECK(status.hook == 0x12345678U);
	CHECK(status.priority == 0x234567);
	CHECK(!status.init_namespace);
	CHECK(status.callbacks == 0x123456789ULL);

	observer.registered = false;
	observer.net = &init_net;
	observer.ops.pf = NFPROTO_IPV4;
	observer.ops.hooknum = NF_INET_PRE_ROUTING;
	observer.ops.priority = -17;
	atomic64_set(&observer.callbacks, 7);
	rk_netfilter_snapshot(&observer, &status);
	CHECK(!status.registered);
	CHECK(status.family == NFPROTO_IPV4);
	CHECK(status.hook == NF_INET_PRE_ROUTING);
	CHECK(status.priority == -17);
	CHECK(status.init_namespace);
	CHECK(status.callbacks == 7);
}
#else
static void check_injected_registration_failure(void)
{
	struct rk_netfilter_observer observer;

	reset_fixture(&observer);
	CHECK(rk_netfilter_start(&observer) == -EAFNOSUPPORT);
	CHECK(event_count == 0);
	CHECK(!observer.registered);
	CHECK(observer.net == NULL);
	CHECK(memcmp(&observer.ops, &(struct nf_hook_ops) { 0 },
		     sizeof(observer.ops)) == 0);
}
#endif

int main(void)
{
#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_NONE
	check_start_and_callback();
	check_registration_error();
	check_stop();
	check_snapshot();
#elif ROOTKITLINGS_FAIL_POINT == RK_FAIL_NETFILTER_REGISTER
	check_injected_registration_failure();
#else
#error "observer fixture built with unsupported failure point"
#endif
	if (failures) {
		fprintf(stderr, "%u Netfilter observer checks failed\n", failures);
		return EXIT_FAILURE;
	}
	puts("Netfilter observer behavior passed");
	return EXIT_SUCCESS;
}
