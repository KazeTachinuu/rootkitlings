// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/string.h>

#include "root_shell.h"

int rk_root_shell_init(struct rk_root_shell *shell, unsigned int timeout_ms)
{
	if (!shell || !timeout_ms || timeout_ms > 300000)
		return -EINVAL;
	memset(shell, 0, sizeof(*shell));
	return -EOPNOTSUPP;
}

int rk_root_shell_request(struct rk_root_shell *shell, __be32 peer_address,
			  u16 port, struct rk_root_shell_ticket *ticket)
{
	(void)shell;
	(void)peer_address;
	(void)port;
	(void)ticket;
	return -EOPNOTSUPP;
}

void rk_root_shell_snapshot(struct rk_root_shell *shell,
			    struct rk_root_shell_status *status)
{
	(void)shell;
	if (status)
		memset(status, 0, sizeof(*status));
}

const char *rk_root_shell_state_name(enum rk_root_shell_state state)
{
	(void)state;
	return "unknown";
}

void rk_root_shell_stop(struct rk_root_shell *shell)
{
	(void)shell;
}
