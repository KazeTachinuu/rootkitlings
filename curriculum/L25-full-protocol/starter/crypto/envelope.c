// SPDX-License-Identifier: GPL-2.0
// crypto/envelope.c - AEAD envelope: seal, open, nonce construction.
#include <crypto/aead.h>
#include <crypto/gcm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include "rootkit.h"

/*
 * TODO: Implement build_nonce (static).
 *
 * Construct a 12-byte nonce from:
 *   bytes 0-7: session_id as big-endian u64
 *   byte 8:    direction (RK_DIR_CLIENT or RK_DIR_SERVER)
 *   bytes 9-11: low 24 bits of sequence number
 */

/*
 * TODO: Implement aead_seal (static).
 *
 * 1. kmalloc buffer of aad_len + pt_len + RK_TAG_LEN.
 * 2. Copy AAD then plaintext into buffer.
 * 3. sg_init_one over the full buffer.
 * 4. aead_request_alloc, set_crypt(pt_len), set_ad(aad_len).
 * 5. crypto_aead_encrypt.
 * 6. On success, memcpy result to out, set *out_len.
 * 7. Free request and buffer.
 */

/*
 * TODO: Implement aead_open (static).
 *
 * 1. Validate total_len >= aad_len + RK_TAG_LEN.
 * 2. ct_plus_tag = total_len - aad_len.
 * 3. kmalloc + memcpy the ciphertext with AAD.
 * 4. sg_init_one, aead_request_alloc.
 * 5. set_crypt(ct_plus_tag), set_ad(aad_len).
 * 6. crypto_aead_decrypt.
 * 7. On success, *out_len = ct_plus_tag - RK_TAG_LEN,
 *    memcpy plaintext from buf + aad_len.
 */

/*
 * TODO: Implement rk_send_sealed.
 *
 * 1. Build envelope header: version=1, direction=RK_DIR_SERVER,
 *    session_id and sequence from session state.
 * 2. build_nonce from session_id, direction, tx_seq.
 * 3. aead_seal with header as AAD, data as plaintext.
 * 4. rk_send_frame the sealed buffer.
 * 5. Increment tx_seq on success.
 */
int rk_send_sealed(struct rk_session *s, const u8 *data, size_t len)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_recv_sealed.
 *
 * 1. kmalloc a RK_FRAME_MAX receive buffer.
 * 2. rk_recv_frame into the buffer.
 * 3. Validate frame_len >= RK_HDR_LEN + RK_TAG_LEN.
 * 4. memcpy the envelope header, extract sequence.
 * 5. Replay check: reject if seq < rx_seq_expected.
 * 6. build_nonce from header fields.
 * 7. aead_open with header as AAD.
 * 8. Validate *out_len <= max_len.
 * 9. Update rx_seq_expected = seq + 1.
 */
int rk_recv_sealed(struct rk_session *s, u8 *out, size_t max_len,
		   size_t *out_len)
{
	return -ENOSYS;
}
