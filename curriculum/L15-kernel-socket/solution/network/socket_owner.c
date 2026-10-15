// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <net/net_namespace.h>

#include "rootkit.h"

static bool rk_config_valid(const struct rk_socket_config *config)
{
	return config->net == &init_net &&
	       config->family == AF_INET &&
	       config->type == SOCK_STREAM &&
	       config->protocol == IPPROTO_TCP &&
	       config->connect_flags == 0 &&
	       config->peer.sin_family == AF_INET &&
	       config->peer.sin_addr.s_addr == htonl(INADDR_LOOPBACK) &&
	       config->peer.sin_port != 0;
}

static bool rk_endpoint_valid(const struct sockaddr_in *local,
			      const struct sockaddr_in *peer,
			      const struct rk_socket_config *config)
{
	return local->sin_family == AF_INET && local->sin_port != 0 &&
	       local->sin_addr.s_addr == htonl(INADDR_LOOPBACK) &&
	       peer->sin_family == AF_INET &&
	       peer->sin_addr.s_addr == config->peer.sin_addr.s_addr &&
	       peer->sin_port == config->peer.sin_port;
}

__must_check int rk_socket_open(struct rk_socket_owner *owner,
				const struct rk_socket_config *config)
{
	struct rk_socket_config requested;
	struct sockaddr_in local = { 0 };
	struct sockaddr_in peer = { 0 };
	struct socket *socket = NULL;
	int local_length;
	int peer_length;
	int error;

	if (!owner || !config)
		return -EINVAL;
	requested = *config;
	if (!rk_config_valid(&requested))
		return -EINVAL;

	mutex_lock(&owner->lock);
	if (owner->socket) {
		error = -EBUSY;
		goto unlock;
	}

	error = sock_create_kern(requested.net, requested.family,
				 requested.type, requested.protocol, &socket);
	if (error)
		goto unlock;
	if (RKLINGS_FAIL_POINT == RK_FAIL_AFTER_CREATE) {
		error = -ECANCELED;
		goto release;
	}

	error = kernel_connect(socket,
			       (struct sockaddr_unsized *)&requested.peer,
			       sizeof(requested.peer), requested.connect_flags);
	if (error)
		goto release;

	local_length = kernel_getsockname(socket, (struct sockaddr *)&local);
	if (local_length < 0) {
		error = local_length;
		goto shutdown;
	}
	peer_length = kernel_getpeername(socket, (struct sockaddr *)&peer);
	if (peer_length < 0) {
		error = peer_length;
		goto shutdown;
	}
	if (local_length != sizeof(local) || peer_length != sizeof(peer) ||
	    !rk_endpoint_valid(&local, &peer, &requested)) {
		error = -EPROTO;
		goto shutdown;
	}
	if (RKLINGS_FAIL_POINT == RK_FAIL_AFTER_CAPTURE) {
		error = -ECANCELED;
		goto shutdown;
	}

	owner->net = requested.net;
	owner->family = requested.family;
	owner->type = requested.type;
	owner->protocol = requested.protocol;
	owner->connect_flags = requested.connect_flags;
	owner->connect_result = 0;
	owner->local_address = local.sin_addr.s_addr;
	owner->local_port = ntohs(local.sin_port);
	owner->peer_address = peer.sin_addr.s_addr;
	owner->peer_port = ntohs(peer.sin_port);
	WRITE_ONCE(owner->socket, socket);
	mutex_unlock(&owner->lock);
	return 0;

shutdown:
	(void)kernel_sock_shutdown(socket, SHUT_RDWR);
release:
	sock_release(socket);
unlock:
	mutex_unlock(&owner->lock);
	return error;
}

void rk_socket_close(struct rk_socket_owner *owner)
{
	struct socket *socket;

	if (!owner)
		return;

	mutex_lock(&owner->lock);
	socket = READ_ONCE(owner->socket);
	if (!socket) {
		mutex_unlock(&owner->lock);
		return;
	}

	WRITE_ONCE(owner->socket, NULL);
	owner->net = NULL;
	owner->family = 0;
	owner->type = 0;
	owner->protocol = 0;
	owner->connect_flags = 0;
	owner->connect_result = 0;
	owner->local_address = 0;
	owner->local_port = 0;
	owner->peer_address = 0;
	owner->peer_port = 0;
	(void)kernel_sock_shutdown(socket, SHUT_RDWR);
	sock_release(socket);
	mutex_unlock(&owner->lock);
}

void rk_socket_snapshot(struct rk_socket_owner *owner,
			struct rk_socket_status *status)
{
	if (!owner || !status)
		return;

	mutex_lock(&owner->lock);
	status->owned = owner->socket != NULL;
	status->init_namespace = owner->net == &init_net;
	status->family = owner->family;
	status->type = owner->type;
	status->protocol = owner->protocol;
	status->connect_flags = owner->connect_flags;
	status->connect_result = owner->connect_result;
	status->local_address = owner->local_address;
	status->local_port = owner->local_port;
	status->peer_address = owner->peer_address;
	status->peer_port = owner->peer_port;
	mutex_unlock(&owner->lock);
}
