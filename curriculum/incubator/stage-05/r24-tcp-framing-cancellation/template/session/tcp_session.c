// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/string.h>

#include "rootkit.h"

__must_check int rk_send_frame_ops(const struct rk_session_ops *ops,
				   void *context, struct socket *socket,
				   const unsigned char *payload, size_t length)
{
	(void)ops;
	(void)context;
	(void)socket;
	(void)payload;
	(void)length;
	return -EOPNOTSUPP;
}

__must_check int rk_receive_frame_ops(const struct rk_session_ops *ops,
				      void *context, struct socket *socket,
				      struct rk_frame *frame)
{
	(void)ops;
	(void)context;
	(void)socket;
	(void)frame;
	return -EOPNOTSUPP;
}

__must_check int rk_session_start_with_ops(
	struct rk_session *session, u16 peer_port,
	const struct rk_session_ops *ops, void *context)
{
	(void)session;
	(void)peer_port;
	(void)ops;
	(void)context;
	return -EOPNOTSUPP;
}

__must_check int rk_session_start(struct rk_session *session, u16 peer_port)
{
	(void)session;
	(void)peer_port;
	return -EOPNOTSUPP;
}

void rk_session_stop(struct rk_session *session)
{
	(void)session;
}

void rk_session_snapshot(struct rk_session *session,
			 struct rk_session_status *status)
{
	(void)session;
	memset(status, 0, sizeof(*status));
}
