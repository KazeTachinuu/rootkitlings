// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

#include "rootkit.h"

#define RK_MISSING_TARGET "__rklings_missing_kprobe_target_7_2__"

static struct rk_state *probe_state;

static int rk_vfs_mkdir_pre(struct kprobe *probe, struct pt_regs *registers)
{
	/* TODO: record this event when actions are enabled. */
	return 0;
}

static int rk_filename_rmdir_pre(struct kprobe *probe,
				 struct pt_regs *registers)
{
	/* TODO: record this event when actions are enabled. */
	return 0;
}

static struct kprobe mkdir_probe = {
	.symbol_name = "vfs_mkdir",
	.pre_handler = rk_vfs_mkdir_pre,
};

static struct kprobe rmdir_probe = {
#if RKLINGS_FAIL_POINT == RK_FAIL_SECOND_PROBE
	.symbol_name = RK_MISSING_TARGET,
#else
	.symbol_name = "filename_rmdir",
#endif
	.pre_handler = rk_filename_rmdir_pre,
};

static struct kprobe *probes[] = {
	&mkdir_probe,
	&rmdir_probe,
};

__must_check int rk_probes_start(struct rk_state *state)
{
	int error;

	if (probe_state || !state)
		return -EINVAL;

	probe_state = state;
	/* TODO: arm all probes and keep the result. */
	(void)probes;
	error = -EOPNOTSUPP;
	if (error)
		probe_state = NULL;
	return error;
}

void rk_probes_stop(void)
{
	if (!probe_state)
		return;

	/* TODO: disarm all probes before releasing borrowed state. */
	probe_state = NULL;
}
