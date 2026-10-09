/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_FRAMING_H
#define ROOTKITLINGS_FRAMING_H

#ifdef __KERNEL__
#include <linux/compiler_attributes.h>
#include <linux/types.h>
#else
#include <stddef.h>
#include <sys/types.h>
#define __must_check
#endif

#define RK_FRAME_MAX 64
#define RK_DRAIN_CHUNK 64

struct socket;

struct rk_transport_ops {
	ssize_t (*send)(struct socket *socket, const unsigned char *bytes,
			size_t length, void *context);
	ssize_t (*receive)(struct socket *socket, unsigned char *bytes,
			   size_t length, void *context);
};

struct rk_frame {
	size_t length;
	unsigned char payload[RK_FRAME_MAX];
};

__must_check int rk_send_frame(const struct rk_transport_ops *ops,
			       void *context, struct socket *socket,
			       const unsigned char *payload, size_t length);
__must_check int rk_receive_frame(const struct rk_transport_ops *ops,
				  void *context, struct socket *socket,
				  struct rk_frame *frame);

#endif
