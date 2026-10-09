// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/net.h>
#include <net/net_namespace.h>

#include "dns_query.h"
#include "transport.h"

int rk_udp_send_loopback(const u8 *packet, size_t length)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(RK_DNS_SIGNAL_PORT),
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	struct msghdr message = {
		.msg_name = &address,
		.msg_namelen = sizeof(address),
	};
	struct socket *socket = NULL;
	struct kvec vector;
	int error;

	if (!packet || length != RK_DNS_SIGNAL_PACKET_LENGTH)
		return -EINVAL;
	error = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, IPPROTO_UDP,
				 &socket);
	if (error)
		return error;
	vector.iov_base = (void *)packet;
	vector.iov_len = length;
	error = kernel_sendmsg(socket, &message, &vector, 1, length);
	sock_release(socket);
	if (error < 0)
		return error;
	return error == length ? 0 : -EIO;
}
