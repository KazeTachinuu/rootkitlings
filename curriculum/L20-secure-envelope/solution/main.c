// SPDX-License-Identifier: GPL-2.0
#include <crypto/aead.h>
#include <crypto/gcm.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#define RK_KEY_LEN	32
#define RK_NONCE_LEN	12
#define RK_TAG_LEN	16
#define RK_HDR_LEN	18

/* Direction constants */
#define RK_DIR_CLIENT	0
#define RK_DIR_SERVER	1

/* Envelope header: version (1) + direction (1) + session_id (8) + sequence (8) */
struct rk_envelope_hdr {
	u8 version;
	u8 direction;
	__be64 session_id;
	__be64 sequence;
} __packed;

/* Per-direction envelope context */
struct rk_envelope_ctx {
	struct crypto_aead *tfm;
	u64 session_id;
	u8 direction;
	u64 tx_seq;
	u64 rx_seq_expected;
};

/* Build a 12-byte GCM nonce: session_id (8) + direction (1) + seq low 3 bytes */
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
 * Seal plaintext into an envelope.
 * Output: header (18) + ciphertext (pt_len) + tag (16).
 * The header is used as AAD (authenticated but not encrypted).
 */
static int rk_envelope_seal(struct rk_envelope_ctx *ctx,
			    const u8 *plaintext, size_t pt_len,
			    u8 *out, size_t *out_len)
{
	struct rk_envelope_hdr hdr;
	u8 nonce[RK_NONCE_LEN];
	struct aead_request *req;
	struct scatterlist sg;
	u8 *buf;
	size_t buf_len;
	int error;

	hdr.version = 1;
	hdr.direction = ctx->direction;
	hdr.session_id = cpu_to_be64(ctx->session_id);
	hdr.sequence = cpu_to_be64(ctx->tx_seq);

	rk_build_nonce(nonce, ctx->session_id, ctx->direction, ctx->tx_seq);

	/* Buffer: AAD (header) + plaintext + tag */
	buf_len = RK_HDR_LEN + pt_len + RK_TAG_LEN;
	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, &hdr, RK_HDR_LEN);
	memcpy(buf + RK_HDR_LEN, plaintext, pt_len);

	sg_init_one(&sg, buf, buf_len);

	req = aead_request_alloc(ctx->tfm, GFP_KERNEL);
	if (!req) {
		kfree(buf);
		return -ENOMEM;
	}

	aead_request_set_crypt(req, &sg, &sg, pt_len, nonce);
	aead_request_set_ad(req, RK_HDR_LEN);

	error = crypto_aead_encrypt(req);
	if (!error) {
		/* Output: header + ciphertext + tag */
		memcpy(out, buf, buf_len);
		*out_len = buf_len;
		ctx->tx_seq++;
	}

	aead_request_free(req);
	kfree(buf);
	return error;
}

/*
 * Open an envelope, verifying header integrity and replay protection.
 * env_len = RK_HDR_LEN + ciphertext_len + RK_TAG_LEN.
 * Returns -EBADMSG on tampered header/payload, -EPROTO on replay.
 */
static int rk_envelope_open(struct rk_envelope_ctx *ctx,
			    const u8 *envelope, size_t env_len,
			    u8 *out, size_t *out_len)
{
	struct rk_envelope_hdr hdr;
	u8 nonce[RK_NONCE_LEN];
	struct aead_request *req;
	struct scatterlist sg;
	u8 *buf;
	size_t ct_plus_tag;
	size_t pt_len;
	u64 seq;
	int error;

	if (env_len < RK_HDR_LEN + RK_TAG_LEN)
		return -EINVAL;

	ct_plus_tag = env_len - RK_HDR_LEN;
	pt_len = ct_plus_tag - RK_TAG_LEN;

	memcpy(&hdr, envelope, RK_HDR_LEN);

	/* Check replay: sequence must be >= expected */
	seq = be64_to_cpu(hdr.sequence);
	if (seq < ctx->rx_seq_expected)
		return -EPROTO;

	rk_build_nonce(nonce, be64_to_cpu(hdr.session_id),
		       hdr.direction, seq);

	buf = kmalloc(env_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, envelope, env_len);

	sg_init_one(&sg, buf, env_len);

	req = aead_request_alloc(ctx->tfm, GFP_KERNEL);
	if (!req) {
		kfree(buf);
		return -ENOMEM;
	}

	aead_request_set_crypt(req, &sg, &sg, ct_plus_tag, nonce);
	aead_request_set_ad(req, RK_HDR_LEN);

	error = crypto_aead_decrypt(req);
	if (!error) {
		memcpy(out, buf + RK_HDR_LEN, pt_len);
		*out_len = pt_len;
		ctx->rx_seq_expected = seq + 1;
	}

	aead_request_free(req);
	kfree(buf);
	return error;
}

static int __init rk_init(void)
{
	struct crypto_aead *tfm;
	struct rk_envelope_ctx sender, receiver;
	u8 key[RK_KEY_LEN];
	u8 envelope[256];
	u8 opened[256];
	size_t env_len, opened_len;
	u64 session_id;
	int error;

	tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	error = crypto_aead_setauthsize(tfm, RK_TAG_LEN);
	if (error)
		goto out;

	get_random_bytes(key, sizeof(key));
	error = crypto_aead_setkey(tfm, key, RK_KEY_LEN);
	if (error)
		goto out;

	get_random_bytes(&session_id, sizeof(session_id));

	/* Initialize sender (client direction) */
	sender.tfm = tfm;
	sender.session_id = session_id;
	sender.direction = RK_DIR_CLIENT;
	sender.tx_seq = 0;
	sender.rx_seq_expected = 0;

	/* Initialize receiver (expects client direction) */
	receiver.tfm = tfm;
	receiver.session_id = session_id;
	receiver.direction = RK_DIR_CLIENT;
	receiver.tx_seq = 0;
	receiver.rx_seq_expected = 0;

	/* Seal and open a test message */
	error = rk_envelope_seal(&sender, "test", 4, envelope, &env_len);
	if (error)
		goto out;

	error = rk_envelope_open(&receiver, envelope, env_len,
				 opened, &opened_len);
	if (error)
		goto out;

	if (opened_len != 4 || memcmp(opened, "test", 4) != 0) {
		error = -EIO;
		goto out;
	}

	/* Replay the same envelope: expect -EPROTO */
	error = rk_envelope_open(&receiver, envelope, env_len,
				 opened, &opened_len);
	if (error == -EPROTO)
		error = 0;
	else
		error = -EIO;

out:
	memzero_explicit(key, sizeof(key));
	crypto_free_aead(tfm);
	return error;
}

static void __exit rk_exit(void)
{
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Secure envelope with replay protection");
