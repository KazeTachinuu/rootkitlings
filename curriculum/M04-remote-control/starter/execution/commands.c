// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - Command handlers for remote control.
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include "rootkit.h"

// Each handler receives the session and an optional argument string.
// Use rk_send_response(s, "text") to reply over the encrypted channel.
// Consult protocol.toml for the exact reply format of each command.

int cmd_status(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_info(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_quit(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_hide_file(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_file(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_hide_module(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_module(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_list(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_download(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_shell(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_shell_status(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}
