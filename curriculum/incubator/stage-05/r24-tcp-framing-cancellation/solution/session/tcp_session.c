// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/uio.h>
#include <net/net_namespace.h>

#include "rootkit.h"

static int rk_send_exact(const struct rk_session_ops *ops, void *context,
			 struct socket *socket, const unsigned char *bytes,
			 size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t result = ops->send(socket, bytes + offset,
					   length - offset, context);

		if (result < 0)
			return result;
		if (result == 0)
			return -EPIPE;
		if ((size_t)result > length - offset)
			return -EPROTO;
		offset += result;
	}
	return 0;
}

static int rk_receive_exact(const struct rk_session_ops *ops, void *context,
			    struct socket *socket, unsigned char *bytes,
			    size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t result = ops->receive(socket, bytes + offset,
					      length - offset, context);

		if (result < 0)
			return result;
		if (result == 0)
			return -ECONNRESET;
		if ((size_t)result > length - offset)
			return -EPROTO;
		offset += result;
	}
	return 0;
}

__must_check int rk_send_frame_ops(const struct rk_session_ops *ops,
				   void *context, struct socket *socket,
				   const unsigned char *payload, size_t length)
{
	unsigned char header[2];
	int error;

	if (!ops || !ops->send || !socket || length > RK_FRAME_MAX ||
	    (length && !payload))
		return -EINVAL;
	header[0] = length >> 8;
	header[1] = length;
	error = rk_send_exact(ops, context, socket, header, sizeof(header));
	if (error)
		return error;
	return rk_send_exact(ops, context, socket, payload, length);
}

__must_check int rk_receive_frame_ops(const struct rk_session_ops *ops,
				      void *context, struct socket *socket,
				      struct rk_frame *frame)
{
	unsigned char scratch[RK_DRAIN_CHUNK];
	unsigned char header[2];
	struct rk_frame received = { };
	size_t remaining;
	int error;

	if (!ops || !ops->receive || !socket || !frame)
		return -EINVAL;
	error = rk_receive_exact(ops, context, socket, header, sizeof(header));
	if (error)
		return error;
	received.length = ((size_t)header[0] << 8) | header[1];
	if (received.length <= RK_FRAME_MAX) {
		error = rk_receive_exact(ops, context, socket, received.payload,
					 received.length);
		if (error)
			return error;
		*frame = received;
		return 0;
	}

	remaining = received.length;
	while (remaining) {
		size_t chunk = min_t(size_t, remaining, sizeof(scratch));

		error = rk_receive_exact(ops, context, socket, scratch, chunk);
		if (error)
			return error;
		remaining -= chunk;
	}
	return -EMSGSIZE;
}

static int rk_real_create(struct socket **socket, void *context)
{
	(void)context;
	return sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP,
				socket);
}

static int rk_real_connect(struct socket *socket,
			   const struct sockaddr_in *peer, void *context)
{
	(void)context;
	return kernel_connect(socket, (struct sockaddr_unsized *)peer,
			      sizeof(*peer), 0);
}

static ssize_t rk_real_send(struct socket *socket,
			    const unsigned char *bytes, size_t length,
			    void *context)
{
	struct msghdr message = { };
	struct kvec vector = {
		.iov_base = (void *)bytes,
		.iov_len = length,
	};

	(void)context;
	return kernel_sendmsg(socket, &message, &vector, 1, length);
}

static ssize_t rk_real_receive(struct socket *socket, unsigned char *bytes,
			       size_t length, void *context)
{
	struct msghdr message = { };
	struct kvec vector = {
		.iov_base = bytes,
		.iov_len = length,
	};

	(void)context;
	return kernel_recvmsg(socket, &message, &vector, 1, length, 0);
}

static int rk_real_shutdown(struct socket *socket, void *context)
{
	(void)context;
	return kernel_sock_shutdown(socket, SHUT_RDWR);
}

static void rk_real_release(struct socket *socket, void *context)
{
	(void)context;
	sock_release(socket);
}

static void rk_real_wake(void *context)
{
	(void)context;
}

static const struct rk_session_ops rk_real_ops = {
	.create = rk_real_create,
	.connect = rk_real_connect,
	.send = rk_real_send,
	.receive = rk_real_receive,
	.shutdown = rk_real_shutdown,
	.release = rk_real_release,
	.wake = rk_real_wake,
};

static void rk_record_error(struct rk_session *session, int error)
{
	mutex_lock(&session->lock);
	if (!session->stopping)
		session->last_error = error;
	mutex_unlock(&session->lock);
}

static int rk_session_worker(void *argument)
{
	struct rk_session *session = argument;
	const struct rk_session_ops *ops = session->ops;
	void *context = session->ops_context;
	struct socket *socket = NULL;
	bool connected = false;
	bool shutdown_here = false;
	int error;

	error = ops->create(&socket, context);
	if (error)
		goto finish;
	if (!socket) {
		error = -EPROTO;
		goto finish;
	}

	mutex_lock(&session->lock);
	if (session->stopping) {
		mutex_unlock(&session->lock);
		ops->release(socket, context);
		socket = NULL;
		goto finish;
	}
	session->active = socket;
	mutex_unlock(&session->lock);

	error = ops->connect(socket, &session->peer, context);
	if (error)
		goto close;
	connected = true;

	while (!kthread_should_stop()) {
		struct rk_frame frame;

		error = rk_receive_frame_ops(ops, context, socket, &frame);
		if (error == -EMSGSIZE) {
			mutex_lock(&session->lock);
			session->oversized++;
			mutex_unlock(&session->lock);
			continue;
		}
		if (error)
			break;
		error = rk_send_frame_ops(ops, context, socket,
					  frame.payload, frame.length);
		if (error)
			break;
		mutex_lock(&session->lock);
		session->frames++;
		mutex_unlock(&session->lock);
	}

close:
	mutex_lock(&session->lock);
	if (session->active == socket)
		session->active = NULL;
	if (connected && !session->shutdown_started) {
		session->shutdown_started = true;
		shutdown_here = true;
	}
	mutex_unlock(&session->lock);
	if (shutdown_here)
		(void)ops->shutdown(socket, context);
	ops->release(socket, context);
	socket = NULL;
finish:
	if (error)
		rk_record_error(session, error);
	mutex_lock(&session->lock);
	session->running = false;
	mutex_unlock(&session->lock);
	wake_up_all(&session->wait);
	return 0;
}

static bool rk_ops_valid(const struct rk_session_ops *ops)
{
	return ops && ops->create && ops->connect && ops->send && ops->receive &&
	       ops->shutdown && ops->release && ops->wake;
}

static int rk_session_start_internal(struct rk_session *session, u16 peer_port,
				     const struct rk_session_ops *ops,
				     void *context, bool inject_failure)
{
	struct task_struct *task;

	if (!session || !peer_port || !rk_ops_valid(ops))
		return -EINVAL;
	if (READ_ONCE(session->initialized)) {
		mutex_lock(&session->lock);
		if (session->task) {
			mutex_unlock(&session->lock);
			return -EBUSY;
		}
		mutex_unlock(&session->lock);
	}
	memset(session, 0, sizeof(*session));
	mutex_init(&session->lock);
	init_waitqueue_head(&session->wait);
	WRITE_ONCE(session->initialized, true);
	session->ops = ops;
	session->ops_context = context;
	session->peer.sin_family = AF_INET;
	session->peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	session->peer.sin_port = htons(peer_port);

	if (inject_failure &&
	    ROOTKITLINGS_FAIL_POINT == RK_FAIL_WORKER_CREATE)
		task = ERR_PTR(-EINTR);
	else
		task = kthread_create(rk_session_worker, session, "rkling_tcp");
	if (IS_ERR(task))
		return PTR_ERR(task);

	mutex_lock(&session->lock);
	session->task = task;
	session->running = true;
	mutex_unlock(&session->lock);
	wake_up_process(task);
	return 0;
}

__must_check int rk_session_start_with_ops(
	struct rk_session *session, u16 peer_port,
	const struct rk_session_ops *ops, void *context)
{
	return rk_session_start_internal(session, peer_port, ops, context, false);
}

__must_check int rk_session_start(struct rk_session *session, u16 peer_port)
{
	return rk_session_start_internal(session, peer_port, &rk_real_ops, NULL,
					 true);
}

void rk_session_stop(struct rk_session *session)
{
	const struct rk_session_ops *ops;
	struct task_struct *task;
	void *context;

	if (!session || !READ_ONCE(session->initialized))
		return;
	mutex_lock(&session->lock);
	task = session->task;
	if (!task) {
		mutex_unlock(&session->lock);
		return;
	}
	session->stopping = true;
	ops = session->ops;
	context = session->ops_context;
	if (session->active && !session->shutdown_started) {
		session->shutdown_started = true;
		(void)ops->shutdown(session->active, context);
	}
	mutex_unlock(&session->lock);

	ops->wake(context);
	wake_up_all(&session->wait);
	(void)kthread_stop(task);

	mutex_lock(&session->lock);
	session->task = NULL;
	session->running = false;
	mutex_unlock(&session->lock);
}

void rk_session_snapshot(struct rk_session *session,
			 struct rk_session_status *status)
{
	if (!session || !status || !READ_ONCE(session->initialized))
		return;
	mutex_lock(&session->lock);
	status->running = session->running;
	status->active = session->active != NULL;
	status->stopping = session->stopping;
	status->peer_port = ntohs(session->peer.sin_port);
	status->frames = session->frames;
	status->oversized = session->oversized;
	status->last_error = session->last_error;
	mutex_unlock(&session->lock);
}
