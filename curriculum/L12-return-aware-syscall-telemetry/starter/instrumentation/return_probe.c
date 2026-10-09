// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "rootkit.h"

#define RK_SELECTED_PATH "/tmp/rootkitlings-rmdir-watch"
#define RK_USER_PATH_SIZE 64
#define RK_MISSING_TARGET "__rootkitlings_missing_kretprobe_target_7_2__"

struct rk_rmdir_instance {
	bool selected;
};

static struct rk_state *probe_state;

static __always_inline long
rk_copy_user_path_nofault(char *destination, const char __user *source,
			  size_t capacity)
{
	size_t index;

	for (index = 0; index < capacity; index++) {
		if (copy_from_user_nofault(destination + index, source + index, 1))
			return -EFAULT;
		if (destination[index] == '\0')
			return index + 1;
	}
	return -ENAMETOOLONG;
}

static __always_inline bool rk_path_is_selected(const char __user *user_path)
{
	char path[RK_USER_PATH_SIZE];
	long length;

	length = rk_copy_user_path_nofault(path, user_path, sizeof(path));
	return length == sizeof(RK_SELECTED_PATH) &&
	       memcmp(path, RK_SELECTED_PATH, sizeof(RK_SELECTED_PATH)) == 0;
}

static int rk_rmdir_entry(struct kretprobe_instance *instance,
			  struct pt_regs *registers)
{
	struct rk_rmdir_instance *observation = (void *)instance->data;
	const char __user *user_path = NULL;

	observation->selected = false;

	/* TODO: admit enabled calls and decode the native syscall path. */
	(void)registers;
	if (user_path)
		observation->selected = rk_path_is_selected(user_path);
	return 0;
}

static int rk_rmdir_return(struct kretprobe_instance *instance,
			   struct pt_regs *registers)
{
	/* TODO: correlate this return with its saved path classification. */
	(void)instance;
	(void)registers;
	return 0;
}

static struct kretprobe rmdir_probe = {
#if ROOTKITLINGS_FAIL_POINT == RK_FAIL_KRETPROBE_TARGET
	.kp.symbol_name = RK_MISSING_TARGET,
#else
	.kp.symbol_name = "__x64_sys_rmdir",
#endif
	.entry_handler = rk_rmdir_entry,
	.handler = rk_rmdir_return,
	.data_size = sizeof(struct rk_rmdir_instance),
	.maxactive = RK_RETURN_CAPACITY,
};

__must_check int rk_return_probe_start(struct rk_state *state)
{
	int error;

	if (probe_state || !state)
		return -EINVAL;

	probe_state = state;
	(void)rmdir_probe;
	/* TODO: register the return probe and preserve its error. */
	error = -EOPNOTSUPP;
	if (error)
		probe_state = NULL;
	return error;
}

void rk_return_probe_stop(void)
{
	if (!probe_state)
		return;

	/* TODO: unregister before releasing the borrowed state. */
	probe_state = NULL;
}

unsigned int rk_return_probe_capacity(void)
{
	return (unsigned int)READ_ONCE(rmdir_probe.maxactive);
}

unsigned int rk_return_probe_missed(void)
{
	return (unsigned int)READ_ONCE(rmdir_probe.nmissed);
}
