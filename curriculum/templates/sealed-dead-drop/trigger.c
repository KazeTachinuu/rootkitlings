// SPDX-License-Identifier: GPL-2.0
#include "dead_drop.h"
#include <linux/kprobes.h>
#include <linux/uaccess.h>

#define PATH_SIZE 128

static long copy_path(char *to, const char __user *from, size_t size)
{
	size_t index;

	for (index = 0; index < size; index++) {
		if (copy_from_user_nofault(to + index, from + index, 1))
			return -EFAULT;
		if (!to[index])
			return index + 1;
	}
	return size;
}

static void handle_knock(const char *path)
{
	/* TODO: unlock or lock the drop for the two exact knock paths. */
}

static int handle_rmdir(struct kprobe *probe, struct pt_regs *registers)
{
	struct pt_regs *syscall_registers = (void *)registers->di;
	const char __user *user_path = (void __user *)syscall_registers->di;
	char path[PATH_SIZE];
	long length = copy_path(path, user_path, sizeof(path));

	if (length > 0 && length < sizeof(path))
		handle_knock(path);
	return 0;
}

static struct kprobe rmdir_probe = {
	.symbol_name = "__x64_sys_rmdir",
	.pre_handler = handle_rmdir,
};

int trigger_start(void)
{
	return register_kprobe(&rmdir_probe);
}

void trigger_stop(void)
{
	unregister_kprobe(&rmdir_probe);
}
