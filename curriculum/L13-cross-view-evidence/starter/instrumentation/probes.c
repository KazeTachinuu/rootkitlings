// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

#include "rootkit.h"

#define RK_MISSING_TARGET "__rklings_missing_kprobe_target_7_2__"

static struct rk_state *probe_state;

static int rk_vfs_mkdir_pre(struct kprobe *probe, struct pt_regs *registers)
{
	if (rk_state_actions_enabled(probe_state))
		rk_state_mark_vfs_mkdir_seen(probe_state);
	return 0;
}

static int rk_filename_rmdir_pre(struct kprobe *probe,
				 struct pt_regs *registers)
{
	if (rk_state_actions_enabled(probe_state))
		rk_state_mark_filename_rmdir_seen(probe_state);
	return 0;
}

static struct kprobe probe_catalog[RK_PROBE_COUNT] = {
	[RK_PROBE_VFS_MKDIR] = {
		.symbol_name = "vfs_mkdir",
		.pre_handler = rk_vfs_mkdir_pre,
	},
	[RK_PROBE_FILENAME_RMDIR] = {
#if RKLINGS_FAIL_POINT == RK_FAIL_SECOND_PROBE
		.symbol_name = RK_MISSING_TARGET,
#else
		.symbol_name = "filename_rmdir",
#endif
		.pre_handler = rk_filename_rmdir_pre,
	},
};

static struct kprobe *probes[] = {
	&probe_catalog[RK_PROBE_VFS_MKDIR],
	&probe_catalog[RK_PROBE_FILENAME_RMDIR],
};

const char *rk_probe_name(enum rk_probe_id id)
{
	if (id >= RK_PROBE_COUNT)
		return NULL;
	return probes[id]->symbol_name;
}

__must_check int rk_probes_start(struct rk_state *state)
{
	int error;

	if (probe_state || !state)
		return -EINVAL;

	probe_state = state;
	error = register_kprobes(probes, ARRAY_SIZE(probes));
	if (error)
		probe_state = NULL;
	return error;
}

void rk_probes_stop(void)
{
	if (!probe_state)
		return;

	unregister_kprobes(probes, ARRAY_SIZE(probes));
	probe_state = NULL;
}
