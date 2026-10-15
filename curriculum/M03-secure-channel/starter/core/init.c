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
	/*
	 * TODO:
	 * 1. Validate and parse rk_key_hex into rk_key with hex2bin.
	 * 2. Zeroize rk_key_hex with memzero_explicit.
	 * 3. Start the agent kthread with kthread_run (name: "kworker/u8:3").
	 * 4. On kthread failure, zeroize rk_key and return the error.
	 */
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	/*
	 * TODO:
	 * 1. Stop the kthread if running.
	 * 2. Zeroize rk_key with memzero_explicit.
	 */
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
