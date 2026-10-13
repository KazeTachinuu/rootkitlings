/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RKLINGS_FRAMING_H
#define RKLINGS_FRAMING_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

#define RK_FRAME_MAX 128

struct socket;

struct rk_frame {
	size_t length;
	unsigned char payload[RK_FRAME_MAX];
};

__must_check int rk_send_frame(struct socket *socket,
			       const unsigned char *payload, size_t length);
__must_check int rk_receive_frame(struct socket *socket,
				  struct rk_frame *frame);

#endif
