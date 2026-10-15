// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/umh.h>
#include <linux/cred.h>
#include <linux/sched/task.h>
#include <linux/slab.h>

static int rk_umh_init(struct subprocess_info *info, struct cred *new)
{
	// Escalate to root before exec.
	return 0;
}

static int __init rk_init(void)
{
	struct subprocess_info *info;

	// Spawn "/bin/sh -c 'id > /tmp/rklings_shell_test'" as root.

	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
