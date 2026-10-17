// SPDX-License-Identifier: GPL-2.0
//
// Your task: implement cmd_launch to run a userspace binary from the kernel
// using call_usermodehelper. The mkdir command "rklings.launch.<path>" passes
// the absolute path as the arg parameter.
//
#include "rootkit.h"
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/umh.h>

// TODO: cmd_launch (validate path starts with "/", build argv+envp, call_usermodehelper with UMH_WAIT_EXEC)
static int cmd_launch(const char *arg)
{
	return -ENOSYS;
}

int rk_launch_init(void)
{
	return rk_cmd_register("launch", cmd_launch);
}
