// SPDX-License-Identifier: GPL-2.0
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include "rklings_hook.h"

// Implement rk_escalate(): allocate root credentials and commit them.
// Use prepare_kernel_cred(&init_task) and commit_creds().

// Implement your RK_ON_MKDIR handler:
// - Match "rklings.e" and call rk_escalate()
// - Return -ENOENT for anything else

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
MODULE_DESCRIPTION("Rklings mkdir escalation");
