// SPDX-License-Identifier: GPL-2.0
/*
 * network/agent.c - Agent kthread: connect, handshake, session loop.
 */
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
 * TODO: Implement rk_handshake.
 *
 * 1. Send encrypted "HELLO" (5 bytes) with rk_send_sealed.
 * 2. Receive and decrypt response with rk_recv_sealed.
 * 3. Verify the response is exactly "HELLO" (5 bytes).
 * 4. Return -EPROTO if the response does not match.
 */

/*
 * TODO: Implement rk_handle_session.
 *
 * Loop while !kthread_should_stop():
 * 1. Allocate a command buffer (RK_FRAME_MAX).
 * 2. Receive a sealed command with rk_recv_sealed.
 * 3. Echo the decrypted command back with rk_send_sealed.
 * 4. Break on any error.
 */

/*
 * TODO: Implement rk_agent_thread.
 *
 * Loop while !kthread_should_stop():
 * 1. Create a TCP socket, connect to INADDR_LOOPBACK:RK_PORT.
 * 2. Allocate AEAD transform: crypto_alloc_aead("gcm(aes)", 0, 0).
 * 3. Set auth size (RK_TAG_LEN) and key (rk_key, RK_KEY_LEN).
 * 4. Generate random session_id, initialize session struct.
 * 5. Run rk_handshake. On failure, clean up and retry.
 * 6. Run rk_handle_session.
 * 7. On disconnect, clean up, sleep RK_RECONNECT_MS, retry.
 */
