// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_SOCKET_FIXTURE_H
#define ROOTKITLINGS_SOCKET_FIXTURE_H

#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#define __must_check
#define READ_ONCE(value) (value)
#define WRITE_ONCE(target, value) ((target) = (value))

typedef uint32_t __be32;
typedef uint16_t u16;

struct net {
	unsigned int identity;
};

extern struct net init_net;

struct socket {
	unsigned int identity;
};

struct sockaddr_unsized {
	unsigned short sa_family;
	char sa_data[];
};

struct mutex {
	unsigned int depth;
};

void rk_test_mutex_lock(struct mutex *lock);
void rk_test_mutex_unlock(struct mutex *lock);

#define mutex_lock(lock) rk_test_mutex_lock(lock)
#define mutex_unlock(lock) rk_test_mutex_unlock(lock)

int sock_create_kern(struct net *net, int family, int type, int protocol,
		     struct socket **result);
int kernel_connect(struct socket *socket, struct sockaddr_unsized *address,
		   int length, int flags);
int kernel_getsockname(struct socket *socket, struct sockaddr *address);
int kernel_getpeername(struct socket *socket, struct sockaddr *address);
int kernel_sock_shutdown(struct socket *socket, int how);
void sock_release(struct socket *socket);

#define RK_FAIL_NONE 0
#define RK_FAIL_AFTER_CREATE 1
#define RK_FAIL_AFTER_CAPTURE 2
#define RK_FAIL_PROC_PUBLISH 3

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

#endif
