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

int rk_root_shell_request(struct rk_root_shell *shell, u16 port)
{
	(void)shell;
	(void)port;
	return -EOPNOTSUPP;
}

void rk_root_shell_stop(struct rk_root_shell *shell)
{
	(void)shell;
}
