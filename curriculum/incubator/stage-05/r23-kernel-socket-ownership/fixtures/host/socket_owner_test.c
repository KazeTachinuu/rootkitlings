// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "rootkit_fixture.h"

enum event {
	EVENT_LOCK = 1,
	EVENT_CREATE,
	EVENT_CONNECT,
	EVENT_GETSOCKNAME,
	EVENT_GETPEERNAME,
	EVENT_SHUTDOWN,
	EVENT_RELEASE,
	EVENT_UNLOCK,
};

struct net init_net = { .identity = 1 };
static struct net other_net = { .identity = 2 };
static struct socket allocated_socket = { .identity = 0x51c };
static struct rk_socket_owner *observed_owner;
static struct rk_socket_owner snapshot_replacement;
static bool replace_on_lock;
static bool expect_clear_on_shutdown;
static int create_result;
static int connect_result;
static int local_result;
static int peer_result;
static int shutdown_result;
static struct sockaddr_in supplied_local;
static struct sockaddr_in supplied_peer;
static struct sockaddr_in expected_connect_peer;
static enum event events[32];
static size_t event_count;
static unsigned int failures;

static void record(enum event event)
{
	if (event_count >= sizeof(events) / sizeof(events[0])) {
		fprintf(stderr, "event log overflow\n");
		exit(2);
	}
	events[event_count++] = event;
}

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static void check_owned_state_is_clear(void)
{
	CHECK(observed_owner != NULL);
	CHECK(observed_owner->socket == NULL);
	CHECK(observed_owner->net == NULL);
	CHECK(observed_owner->family == 0);
	CHECK(observed_owner->type == 0);
	CHECK(observed_owner->protocol == 0);
	CHECK(observed_owner->connect_flags == 0);
	CHECK(observed_owner->connect_result == 0);
	CHECK(observed_owner->local_address == 0);
	CHECK(observed_owner->local_port == 0);
	CHECK(observed_owner->peer_address == 0);
	CHECK(observed_owner->peer_port == 0);
}

void rk_test_mutex_lock(struct mutex *lock)
{
	record(EVENT_LOCK);
	CHECK(lock != NULL);
	CHECK(lock->depth == 0);
	lock->depth++;
	if (replace_on_lock && observed_owner) {
		struct mutex saved = observed_owner->lock;

		*observed_owner = snapshot_replacement;
		observed_owner->lock = saved;
	}
}

void rk_test_mutex_unlock(struct mutex *lock)
{
	record(EVENT_UNLOCK);
	CHECK(lock != NULL);
	CHECK(lock->depth == 1);
	if (lock->depth == 1)
		lock->depth--;
}

static void check_acquisition_context(void)
{
	CHECK(observed_owner != NULL);
	CHECK(observed_owner->lock.depth == 1);
	CHECK(observed_owner->socket == NULL);
}

int sock_create_kern(struct net *net, int family, int type, int protocol,
		     struct socket **result)
{
	record(EVENT_CREATE);
	check_acquisition_context();
	CHECK(net == &init_net);
	CHECK(family == AF_INET);
	CHECK(type == SOCK_STREAM);
	CHECK(protocol == IPPROTO_TCP);
	CHECK(result != NULL);
	if (create_result == 0)
		*result = &allocated_socket;
	return create_result;
}

int kernel_connect(struct socket *socket, struct sockaddr_unsized *address,
		   int length, int flags)
{
	const struct sockaddr_in *peer = (const struct sockaddr_in *)address;

	record(EVENT_CONNECT);
	check_acquisition_context();
	CHECK(socket == &allocated_socket);
	CHECK(address != NULL);
	CHECK(length == (int)sizeof(*peer));
	CHECK(flags == 0);
	CHECK(memcmp(peer, &expected_connect_peer, sizeof(*peer)) == 0);
	return connect_result;
}

int kernel_getsockname(struct socket *socket, struct sockaddr *address)
{
	record(EVENT_GETSOCKNAME);
	check_acquisition_context();
	CHECK(socket == &allocated_socket);
	CHECK(address != NULL);
	memcpy(address, &supplied_local, sizeof(supplied_local));
	return local_result;
}

int kernel_getpeername(struct socket *socket, struct sockaddr *address)
{
	record(EVENT_GETPEERNAME);
	check_acquisition_context();
	CHECK(socket == &allocated_socket);
	CHECK(address != NULL);
	memcpy(address, &supplied_peer, sizeof(supplied_peer));
	return peer_result;
}

int kernel_sock_shutdown(struct socket *socket, int how)
{
	record(EVENT_SHUTDOWN);
	CHECK(observed_owner != NULL);
	CHECK(observed_owner->lock.depth == 1);
	CHECK(socket == &allocated_socket);
	CHECK(how == SHUT_RDWR);
	if (expect_clear_on_shutdown)
		check_owned_state_is_clear();
	else
		CHECK(observed_owner->socket == NULL);
	return shutdown_result;
}

void sock_release(struct socket *socket)
{
	record(EVENT_RELEASE);
	CHECK(observed_owner != NULL);
	CHECK(observed_owner->lock.depth == 1);
	CHECK(socket == &allocated_socket);
}

static struct rk_socket_config valid_config(unsigned int port)
{
	struct rk_socket_config config = {
		.net = &init_net,
		.family = AF_INET,
		.type = SOCK_STREAM,
		.protocol = IPPROTO_TCP,
		.connect_flags = 0,
		.peer = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
			.sin_port = htons((unsigned short)port),
		},
	};

	return config;
}

static void reset_fixture(struct rk_socket_owner *owner, unsigned int port)
{
	memset(owner, 0, sizeof(*owner));
	observed_owner = owner;
	replace_on_lock = false;
	expect_clear_on_shutdown = false;
	create_result = 0;
	connect_result = 0;
	local_result = (int)sizeof(supplied_local);
	peer_result = (int)sizeof(supplied_peer);
	shutdown_result = 0;
	supplied_local = (struct sockaddr_in) {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons((unsigned short)(port + 1000)),
	};
	supplied_peer = (struct sockaddr_in) {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons((unsigned short)port),
	};
	expected_connect_peer = supplied_peer;
	event_count = 0;
}

static void expect_events(const enum event *expected, size_t count)
{
	size_t index;

	CHECK(event_count == count);
	for (index = 0; index < count && index < event_count; index++)
		CHECK(events[index] == expected[index]);
}

static void __attribute__((unused)) check_success(unsigned int port)
{
	static const enum event expected[] = {
		EVENT_LOCK, EVENT_CREATE, EVENT_CONNECT, EVENT_GETSOCKNAME,
		EVENT_GETPEERNAME, EVENT_UNLOCK,
	};
	struct rk_socket_owner owner;
	struct rk_socket_config config = valid_config(port);
	int result;

	reset_fixture(&owner, port);
	result = rk_socket_open(&owner, &config);
	CHECK(result == 0);
	expect_events(expected, sizeof(expected) / sizeof(expected[0]));
	CHECK(owner.lock.depth == 0);
	CHECK(owner.socket == &allocated_socket);
	CHECK(owner.net == config.net);
	CHECK(owner.family == config.family);
	CHECK(owner.type == config.type);
	CHECK(owner.protocol == config.protocol);
	CHECK(owner.connect_flags == config.connect_flags);
	CHECK(owner.connect_result == 0);
	CHECK(owner.local_address == supplied_local.sin_addr.s_addr);
	CHECK(owner.local_port == ntohs(supplied_local.sin_port));
	CHECK(owner.peer_address == supplied_peer.sin_addr.s_addr);
	CHECK(owner.peer_port == ntohs(supplied_peer.sin_port));
}

static void __attribute__((unused)) check_invalid_configs(void)
{
	struct rk_socket_owner owner;
	struct rk_socket_config config;
	struct rk_socket_config invalid[8];
	size_t index;

	reset_fixture(&owner, 26001);
	config = valid_config(26001);
	CHECK(rk_socket_open(NULL, &config) == -EINVAL);
	CHECK(rk_socket_open(&owner, NULL) == -EINVAL);
	CHECK(event_count == 0);

	for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++)
		invalid[index] = config;
	invalid[0].net = &other_net;
	invalid[1].family = AF_INET6;
	invalid[2].type = SOCK_DGRAM;
	invalid[3].protocol = IPPROTO_UDP;
	invalid[4].connect_flags = 1;
	invalid[5].peer.sin_family = AF_INET6;
	invalid[6].peer.sin_addr.s_addr = htonl(0x7f000002U);
	invalid[7].peer.sin_port = 0;
	for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
		reset_fixture(&owner, 26001);
		CHECK(rk_socket_open(&owner, &invalid[index]) == -EINVAL);
		CHECK(event_count == 0);
		check_owned_state_is_clear();
	}
}

static void __attribute__((unused)) check_busy(void)
{
	static const enum event expected[] = { EVENT_LOCK, EVENT_UNLOCK };
	struct rk_socket_owner owner;
	struct rk_socket_config config = valid_config(26002);
	struct rk_socket_owner before;

	reset_fixture(&owner, 26002);
	owner.socket = &allocated_socket;
	owner.net = &other_net;
	owner.peer_port = 99;
	before = owner;
	CHECK(rk_socket_open(&owner, &config) == -EBUSY);
	expect_events(expected, sizeof(expected) / sizeof(expected[0]));
	before.lock.depth = 0;
	CHECK(memcmp(&owner, &before, sizeof(owner)) == 0);
}

static void __attribute__((unused)) check_open_failure(int phase, int error)
{
	static const enum event create_events[] = {
		EVENT_LOCK, EVENT_CREATE, EVENT_UNLOCK,
	};
	static const enum event connect_events[] = {
		EVENT_LOCK, EVENT_CREATE, EVENT_CONNECT, EVENT_RELEASE,
		EVENT_UNLOCK,
	};
	static const enum event local_events[] = {
		EVENT_LOCK, EVENT_CREATE, EVENT_CONNECT, EVENT_GETSOCKNAME,
		EVENT_SHUTDOWN, EVENT_RELEASE, EVENT_UNLOCK,
	};
	static const enum event peer_events[] = {
		EVENT_LOCK, EVENT_CREATE, EVENT_CONNECT, EVENT_GETSOCKNAME,
		EVENT_GETPEERNAME, EVENT_SHUTDOWN, EVENT_RELEASE, EVENT_UNLOCK,
	};
	struct rk_socket_owner owner;
	struct rk_socket_config config = valid_config(26003);

	reset_fixture(&owner, 26003);
	if (phase == 1)
		create_result = error;
	else if (phase == 2)
		connect_result = error;
	else if (phase == 3)
		local_result = error;
	else
		peer_result = error;
	CHECK(rk_socket_open(&owner, &config) == error);
	check_owned_state_is_clear();
	if (phase == 1)
		expect_events(create_events, sizeof(create_events) / sizeof(create_events[0]));
	else if (phase == 2)
		expect_events(connect_events, sizeof(connect_events) / sizeof(connect_events[0]));
	else if (phase == 3)
		expect_events(local_events, sizeof(local_events) / sizeof(local_events[0]));
	else
		expect_events(peer_events, sizeof(peer_events) / sizeof(peer_events[0]));
}

static void __attribute__((unused)) check_bad_endpoint(int case_number)
{
	static const enum event expected[] = {
		EVENT_LOCK, EVENT_CREATE, EVENT_CONNECT, EVENT_GETSOCKNAME,
		EVENT_GETPEERNAME, EVENT_SHUTDOWN, EVENT_RELEASE, EVENT_UNLOCK,
	};
	struct rk_socket_owner owner;
	struct rk_socket_config config = valid_config(26004);

	reset_fixture(&owner, 26004);
	switch (case_number) {
	case 0: local_result--; break;
	case 1: peer_result++; break;
	case 2: supplied_local.sin_family = AF_INET6; break;
	case 3: supplied_local.sin_port = 0; break;
	case 4: supplied_local.sin_addr.s_addr = htonl(0x7f000002U); break;
	case 5: supplied_peer.sin_family = AF_INET6; break;
	case 6: supplied_peer.sin_addr.s_addr = htonl(0x7f000002U); break;
	case 7: supplied_peer.sin_port = htons(26005); break;
	default: abort();
	}
	shutdown_result = -EIO;
	CHECK(rk_socket_open(&owner, &config) == -EPROTO);
	check_owned_state_is_clear();
	expect_events(expected, sizeof(expected) / sizeof(expected[0]));
}

static void fill_owned(struct rk_socket_owner *owner)
{
	owner->socket = &allocated_socket;
	owner->net = &init_net;
	owner->family = AF_INET;
	owner->type = SOCK_STREAM;
	owner->protocol = IPPROTO_TCP;
	owner->connect_flags = 17;
	owner->connect_result = -19;
	owner->local_address = htonl(0x7f000001U);
	owner->local_port = 31001;
	owner->peer_address = htonl(0x7f000001U);
	owner->peer_port = 31002;
}

static void __attribute__((unused)) check_close(void)
{
	static const enum event empty_events[] = { EVENT_LOCK, EVENT_UNLOCK };
	static const enum event owned_events[] = {
		EVENT_LOCK, EVENT_SHUTDOWN, EVENT_RELEASE, EVENT_UNLOCK,
	};
	struct rk_socket_owner owner;

	reset_fixture(&owner, 26005);
	rk_socket_close(NULL);
	CHECK(event_count == 0);
	rk_socket_close(&owner);
	expect_events(empty_events, sizeof(empty_events) / sizeof(empty_events[0]));

	reset_fixture(&owner, 26005);
	fill_owned(&owner);
	shutdown_result = -EIO;
	expect_clear_on_shutdown = true;
	rk_socket_close(&owner);
	check_owned_state_is_clear();
	expect_events(owned_events, sizeof(owned_events) / sizeof(owned_events[0]));
}

static void __attribute__((unused)) check_snapshot(void)
{
	static const enum event expected[] = { EVENT_LOCK, EVENT_UNLOCK };
	struct rk_socket_owner owner;
	struct rk_socket_status status;

	reset_fixture(&owner, 26006);
	memset(&status, 0xa5, sizeof(status));
	rk_socket_snapshot(NULL, &status);
	CHECK(event_count == 0);
	rk_socket_snapshot(&owner, NULL);
	CHECK(event_count == 0);

	fill_owned(&snapshot_replacement);
	snapshot_replacement.family = 0x1234;
	snapshot_replacement.type = 0x2345;
	snapshot_replacement.protocol = 0x3456;
	snapshot_replacement.connect_flags = 0x4567;
	snapshot_replacement.connect_result = -0x567;
	snapshot_replacement.local_address = htonl(0x7f010203U);
	snapshot_replacement.local_port = 41001;
	snapshot_replacement.peer_address = htonl(0x7f040506U);
	snapshot_replacement.peer_port = 41002;
	replace_on_lock = true;
	memset(&status, 0, sizeof(status));
	rk_socket_snapshot(&owner, &status);
	expect_events(expected, sizeof(expected) / sizeof(expected[0]));
	CHECK(status.owned);
	CHECK(status.init_namespace);
	CHECK(status.family == snapshot_replacement.family);
	CHECK(status.type == snapshot_replacement.type);
	CHECK(status.protocol == snapshot_replacement.protocol);
	CHECK(status.connect_flags == snapshot_replacement.connect_flags);
	CHECK(status.connect_result == snapshot_replacement.connect_result);
	CHECK(status.local_address == snapshot_replacement.local_address);
	CHECK(status.local_port == snapshot_replacement.local_port);
	CHECK(status.peer_address == snapshot_replacement.peer_address);
	CHECK(status.peer_port == snapshot_replacement.peer_port);
}

#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_AFTER_CREATE || \
	ROOTKITLINGS_FAIL_POINT == RK_FAIL_AFTER_CAPTURE
static void check_injected_failure(void)
{
	struct rk_socket_owner owner;
	struct rk_socket_config config = valid_config(26007);

	reset_fixture(&owner, 26007);
	CHECK(rk_socket_open(&owner, &config) == -ECANCELED);
	check_owned_state_is_clear();
#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_AFTER_CREATE
	{
		static const enum event expected[] = {
			EVENT_LOCK, EVENT_CREATE, EVENT_RELEASE, EVENT_UNLOCK,
		};
		expect_events(expected, sizeof(expected) / sizeof(expected[0]));
	}
#elif ROOTKITLINGS_FAIL_POINT == RK_FAIL_AFTER_CAPTURE
	{
		static const enum event expected[] = {
			EVENT_LOCK, EVENT_CREATE, EVENT_CONNECT, EVENT_GETSOCKNAME,
			EVENT_GETPEERNAME, EVENT_SHUTDOWN, EVENT_RELEASE, EVENT_UNLOCK,
		};
		expect_events(expected, sizeof(expected) / sizeof(expected[0]));
	}
#else
#error "injected-failure fixture built with unsupported point"
#endif
}
#endif

int main(void)
{
#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_NONE
	int index;

	check_invalid_configs();
	check_busy();
	check_open_failure(1, -EACCES);
	check_open_failure(2, -ECONNREFUSED);
	check_open_failure(3, -ENOBUFS);
	check_open_failure(4, -EHOSTUNREACH);
	for (index = 0; index < 8; index++)
		check_bad_endpoint(index);
	check_success(27001);
	check_success(38001);
	check_close();
	check_snapshot();
#else
	check_injected_failure();
#endif
	if (failures != 0) {
		fprintf(stderr, "%u socket ownership checks failed\n", failures);
		return 1;
	}
	puts("socket ownership behavior passed");
	return 0;
}
