// SPDX-License-Identifier: GPL-2.0
// execution/dispatch.c - Function pointer dispatch table for commands.
#include <linux/string.h>
#include "rootkit.h"

// Build the dispatch table and dispatcher.
// Map command prefixes to handler functions.
// Prefixes with a trailing space take an argument (everything after the space).
// Prefixes without a trailing space require an exact-length match.
// Longer prefixes must appear first so greedy match works correctly.

int rk_send_response(struct rk_session *s, const char *resp)
{
	return -ENOSYS;
}

int rk_dispatch(struct rk_session *s, const u8 *buf, size_t len)
{
	return -ENOSYS;
}
