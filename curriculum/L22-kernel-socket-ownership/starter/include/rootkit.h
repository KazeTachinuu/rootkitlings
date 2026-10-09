// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include <linux/in.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define RK_FAIL_NONE 0
#define RK_FAIL_AFTER_CREATE 1
#define RK_FAIL_AFTER_CAPTURE 2
#define RK_FAIL_PROC_PUBLISH 3

struct net;
struct socket;

struct rk_socket_config {
	struct net *net;
	int family;
	int type;
	int protocol;
	int connect_flags;
	struct sockaddr_in peer;
};

struct rk_socket_owner {
	struct mutex lock;
	struct socket *socket;
	struct net *net;
	int family;
	int type;
	int protocol;
	int connect_flags;
	int connect_result;
	__be32 local_address;
	u16 local_port;
	__be32 peer_address;
	u16 peer_port;
};

struct rk_socket_status {
	bool owned;
	bool init_namespace;
	int family;
	int type;
	int protocol;
	int connect_flags;
	int connect_result;
	__be32 local_address;
	u16 local_port;
	__be32 peer_address;
	u16 peer_port;
};

__must_check int rk_socket_open(struct rk_socket_owner *owner,
				const struct rk_socket_config *config);
void rk_socket_close(struct rk_socket_owner *owner);
void rk_socket_snapshot(struct rk_socket_owner *owner,
			struct rk_socket_status *status);

__must_check int rk_proc_publish(struct rk_socket_owner *owner);
void rk_proc_unpublish(void);

#endif
