// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/uio.h>
#include <net/net_namespace.h>

#include "agent.h"

static ssize_t rk_socket_send(struct socket *socket,
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

static ssize_t rk_socket_receive(struct socket *socket, unsigned char *bytes,
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

static const struct rk_transport_ops rk_transport = {
	.send = rk_socket_send,
	.receive = rk_socket_receive,
};

static void rk_count_protocol_error(struct rk_agent *agent)
{
	mutex_lock(&agent->lock);
	agent->protocol_errors++;
	mutex_unlock(&agent->lock);
}

static int rk_agent_command(struct rk_agent *agent, struct socket *socket,
			    const struct rk_frame *frame, bool *close_session)
{
	static const unsigned char error_reply[] = "ERR";
	static const unsigned char info_reply[] = "INFO rootkitlings";
	static const unsigned char pong_reply[] = "PONG";
	unsigned char reply[RK_FRAME_MAX];
	struct rk_command command;
	size_t reply_length;
	int error;

	*close_session = false;
	error = rk_command_parse(frame->payload, frame->length, &command);
	mutex_lock(&agent->lock);
	agent->commands++;
	if (error)
		agent->protocol_errors++;
	mutex_unlock(&agent->lock);
	if (error)
		return rk_send_frame(&rk_transport, NULL, socket, error_reply,
				     sizeof(error_reply) - 1);

	switch (command.kind) {
	case RK_COMMAND_PING:
		return rk_send_frame(&rk_transport, NULL, socket, pong_reply,
				     sizeof(pong_reply) - 1);
	case RK_COMMAND_INFO:
		return rk_send_frame(&rk_transport, NULL, socket, info_reply,
				     sizeof(info_reply) - 1);
	case RK_COMMAND_MARK:
		mutex_lock(&agent->lock);
		memcpy(agent->marker, command.marker, command.marker_length + 1);
		mutex_unlock(&agent->lock);
		memcpy(reply, "MARKED ", 7);
		memcpy(reply + 7, command.marker, command.marker_length);
		reply_length = 7 + command.marker_length;
		return rk_send_frame(&rk_transport, NULL, socket, reply,
				     reply_length);
	case RK_COMMAND_QUIT:
		*close_session = true;
		return 0;
	case RK_COMMAND_INVALID:
	default:
		return -EPROTO;
	}
}

static int rk_agent_serve(struct rk_agent *agent, struct socket *socket)
{
	for (;;) {
		struct rk_frame frame;
		bool close_session;
		int error;

		if (kthread_should_stop())
			return 0;
		error = rk_receive_frame(&rk_transport, NULL, socket, &frame);
		if (error == -EMSGSIZE) {
			static const unsigned char reply[] = "ERR";

			rk_count_protocol_error(agent);
			error = rk_send_frame(&rk_transport, NULL, socket, reply,
					      sizeof(reply) - 1);
			if (!error)
				continue;
		}
		if (error)
			return error;
		error = rk_agent_command(agent, socket, &frame, &close_session);
		if (error || close_session)
			return error;
	}
}

static int rk_agent_attempt(struct rk_agent *agent)
{
	struct socket *socket = NULL;
	bool connected = false;
	bool shutdown_here = false;
	int error;

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP,
				 &socket);
	if (error)
		return error;
	if (!socket)
		return -EPROTO;

	mutex_lock(&agent->lock);
	if (agent->stopping) {
		mutex_unlock(&agent->lock);
		sock_release(socket);
		return 0;
	}
	agent->active = socket;
	agent->shutdown_started = false;
	mutex_unlock(&agent->lock);

	error = kernel_connect(socket,
			       (struct sockaddr_unsized *)&agent->peer,
			       sizeof(agent->peer), 0);
	if (error)
		goto close;
	connected = true;
	mutex_lock(&agent->lock);
	agent->connected = true;
	agent->connections++;
	agent->retry_delay_ms = agent->retry_base_ms;
	mutex_unlock(&agent->lock);
	error = rk_agent_serve(agent, socket);

close:
	mutex_lock(&agent->lock);
	if (agent->active == socket)
		agent->active = NULL;
	agent->connected = false;
	if (connected && !agent->shutdown_started) {
		agent->shutdown_started = true;
		shutdown_here = true;
	}
	mutex_unlock(&agent->lock);
	if (shutdown_here)
		(void)kernel_sock_shutdown(socket, SHUT_RDWR);
	sock_release(socket);
	return error;
}

static int rk_agent_worker(void *argument)
{
	struct rk_agent *agent = argument;

	while (!kthread_should_stop()) {
		unsigned int delay;
		long waited;
		int error;

		mutex_lock(&agent->lock);
		agent->attempts++;
		mutex_unlock(&agent->lock);
		error = rk_agent_attempt(agent);
		if (kthread_should_stop())
			break;
		mutex_lock(&agent->lock);
		if (error)
			agent->transport_errors++;
		delay = agent->retry_delay_ms;
		agent->retries++;
		if (error)
			agent->retry_delay_ms = rk_retry_next(
				agent->retry_delay_ms, agent->retry_base_ms,
				agent->retry_max_ms);
		else
			agent->retry_delay_ms = agent->retry_base_ms;
		mutex_unlock(&agent->lock);
		waited = wait_event_interruptible_timeout(
			agent->retry_wait, kthread_should_stop(),
			msecs_to_jiffies(delay));
		(void)waited;
	}
	return 0;
}

int rk_agent_start(struct rk_agent *agent, u16 peer_port,
		   unsigned int retry_base_ms, unsigned int retry_max_ms)
{
	struct task_struct *task;

	if (!agent || !peer_port || !retry_base_ms ||
	    retry_base_ms > retry_max_ms || retry_max_ms > 5000)
		return -EINVAL;
	memset(agent, 0, sizeof(*agent));
	mutex_init(&agent->lock);
	init_waitqueue_head(&agent->retry_wait);
	agent->peer.sin_family = AF_INET;
	agent->peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	agent->peer.sin_port = htons(peer_port);
	agent->retry_base_ms = retry_base_ms;
	agent->retry_max_ms = retry_max_ms;
	agent->retry_delay_ms = retry_base_ms;
	agent->initialized = true;
	task = kthread_create(rk_agent_worker, agent, "rkling_agent");
	if (IS_ERR(task)) {
		agent->initialized = false;
		return PTR_ERR(task);
	}
	mutex_lock(&agent->lock);
	agent->task = task;
	mutex_unlock(&agent->lock);
	wake_up_process(task);
	return 0;
}

void rk_agent_stop(struct rk_agent *agent)
{
	struct task_struct *task;

	if (!agent || !READ_ONCE(agent->initialized))
		return;
	mutex_lock(&agent->lock);
	task = agent->task;
	if (!task) {
		mutex_unlock(&agent->lock);
		return;
	}
	agent->stopping = true;
	if (agent->active && !agent->shutdown_started) {
		agent->shutdown_started = true;
		(void)kernel_sock_shutdown(agent->active, SHUT_RDWR);
	}
	mutex_unlock(&agent->lock);
	wake_up_interruptible(&agent->retry_wait);
	(void)kthread_stop(task);
	mutex_lock(&agent->lock);
	agent->task = NULL;
	agent->connected = false;
	mutex_unlock(&agent->lock);
}

void rk_agent_snapshot(struct rk_agent *agent, struct rk_agent_status *status)
{
	if (!status)
		return;
	memset(status, 0, sizeof(*status));
	if (!agent || !READ_ONCE(agent->initialized))
		return;
	mutex_lock(&agent->lock);
	status->running = agent->task != NULL;
	status->connected = agent->connected;
	status->stopping = agent->stopping;
	status->peer_port = ntohs(agent->peer.sin_port);
	status->retry_base_ms = agent->retry_base_ms;
	status->retry_max_ms = agent->retry_max_ms;
	status->retry_delay_ms = agent->retry_delay_ms;
	status->attempts = agent->attempts;
	status->connections = agent->connections;
	status->commands = agent->commands;
	status->retries = agent->retries;
	status->transport_errors = agent->transport_errors;
	status->protocol_errors = agent->protocol_errors;
	memcpy(status->marker, agent->marker, sizeof(status->marker));
	mutex_unlock(&agent->lock);
}
