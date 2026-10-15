// SPDX-License-Identifier: GPL-2.0
/*
 * crypto/envelope.c - AEAD envelope: seal, open, nonce construction.
 */
#include <crypto/aead.h>
#include <crypto/gcm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "rootkit.h"

/* Build a 12-byte nonce from session ID, direction, and sequence. */
static void build_nonce(u8 *nonce, u64 session_id, u8 direction, u64 seq)
{
	__be64 sid = cpu_to_be64(session_id);

	memcpy(nonce, &sid, 8);
	nonce[8] = direction;
	nonce[9] = (seq >> 16) & 0xFF;
	nonce[10] = (seq >> 8) & 0xFF;
	nonce[11] = seq & 0xFF;
}

/*
 * TODO: Implement aead_seal.
 *
 * Encrypt plaintext with AAD using AES-256-GCM (in-place with scatterlist).
 *
 * 1. Allocate buf of size aad_len + pt_len + RK_TAG_LEN.
 * 2. Copy AAD (if any) then plaintext into buf.
 * 3. sg_init_one over the full buf.
 * 4. aead_request_alloc, set_crypt (pt_len bytes to encrypt), set_ad.
 * 5. crypto_aead_encrypt. On success, copy buf to out, set *out_len.
 * 6. Free request and buf.
 */

/*
 * TODO: Implement aead_open.
 *
 * Decrypt and verify ciphertext with AAD using AES-256-GCM.
 *
 * 1. Verify total_len >= aad_len + RK_TAG_LEN.
 * 2. ct_plus_tag = total_len - aad_len.
 * 3. Allocate buf, copy ct_with_aad into it.
 * 4. sg_init_one, aead_request_alloc, set_crypt (ct_plus_tag), set_ad.
 * 5. crypto_aead_decrypt. On success, *out_len = ct_plus_tag - RK_TAG_LEN,
 *    copy plaintext from buf + aad_len to out.
 * 6. Free request and buf.
 */

/*
 * TODO: Implement rk_send_sealed.
 *
 * 1. Build the 18-byte envelope header (version=1, direction=RK_DIR_SERVER,
 *    session_id and sequence from s->tx_seq).
 * 2. Construct nonce with build_nonce.
 * 3. Allocate sealed buffer (RK_HDR_LEN + len + RK_TAG_LEN).
 * 4. Call aead_seal with header as AAD and data as plaintext.
 * 5. Send the sealed envelope as a frame with rk_send_frame.
 * 6. Increment s->tx_seq on success.
 */

/*
 * TODO: Implement rk_recv_sealed.
 *
 * 1. Allocate frame buffer (RK_FRAME_MAX), receive with rk_recv_frame.
 * 2. Verify frame_len >= RK_HDR_LEN + RK_TAG_LEN.
 * 3. Parse the 18-byte header from the frame.
 * 4. Replay check: sequence >= s->rx_seq_expected (reject with -EPROTO).
 * 5. Reconstruct nonce from header fields with build_nonce.
 * 6. Call aead_open to decrypt and verify.
 * 7. Verify *out_len <= max_len.
 * 8. Update s->rx_seq_expected = seq + 1.
 */
