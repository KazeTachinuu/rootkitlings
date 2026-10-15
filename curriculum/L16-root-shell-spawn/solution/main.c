// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/umh.h>
#include <linux/cred.h>
#include <linux/sched/task.h>
#include <linux/slab.h>

// Escalate the spawned process to root
static int rk_umh_init(struct subprocess_info *info, struct cred *new)
{
	commit_creds(prepare_kernel_cred(&init_task));
	return 0;
}

static int __init rk_init(void)
{
	struct subprocess_info *info;

	char *argv[] = {
		"/bin/sh", "-c",
		"id > /tmp/rklings_shell_test",
		NULL
	};
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		NULL
	};

	info = call_usermodehelper_setup(argv[0], argv, envp,
					 GFP_KERNEL,
					 rk_umh_init, NULL, NULL);
	if (!info)
		return -ENOMEM;

	return call_usermodehelper_exec(info, UMH_WAIT_EXEC);
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Spawn a root shell from kernel");
