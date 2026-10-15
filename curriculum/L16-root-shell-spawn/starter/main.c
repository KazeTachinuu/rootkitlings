// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/umh.h>
#include <linux/cred.h>
#include <linux/slab.h>

// Escalate the spawned process to root
static int rk_umh_init(struct subprocess_info *info, struct cred *new)
{
	// TODO: Set root credentials with commit_creds(prepare_kernel_cred(NULL)).
	// pr_info("rklings: escalate to root here\n");
	return 0;
}

static int __init rk_init(void)
{
	struct subprocess_info *info;

	// TODO: Define argv: { "/bin/sh", "-c", "id > /tmp/rklings_shell_test", NULL }
	// TODO: Define envp: { "HOME=/", "PATH=/sbin:/bin:/usr/sbin:/usr/bin", NULL }

	// TODO: Call call_usermodehelper_setup with argv[0], argv, envp,
	//   GFP_KERNEL, rk_umh_init as the init callback, NULL for cleanup
	//   and data. Check for NULL return.
	// pr_info("rklings: set up usermodehelper here\n");

	// TODO: Call call_usermodehelper_exec(info, UMH_WAIT_EXEC).

	return 0;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
