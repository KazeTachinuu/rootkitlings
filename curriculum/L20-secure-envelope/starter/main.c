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

#define RK_DIR_CLIENT	0
#define RK_DIR_SERVER	1

/*
 * Envelope header: 18 bytes total.
 *   version    (u8)   : protocol version, always 1
 *   direction  (u8)   : 0 = client-to-server, 1 = server-to-client
 *   session_id (__be64): random per-connection identifier
 *   sequence   (__be64): per-direction message counter
 */
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

/*
 * TODO: Implement rk_build_nonce.
 *
 * Build a 12-byte GCM nonce from:
 *   bytes 0-7  : session_id in big-endian (cpu_to_be64)
 *   byte 8     : direction
 *   bytes 9-11 : low 3 bytes of sequence counter
 *
 * void rk_build_nonce(u8 *nonce, u64 session_id, u8 direction, u64 seq)
 */

/*
 * TODO: Implement rk_envelope_seal.
 *
 * Seal plaintext into an authenticated envelope.
 * 1. Build the 18-byte header from ctx fields and current tx_seq.
 * 2. Construct the nonce with rk_build_nonce.
 * 3. Allocate buffer: RK_HDR_LEN + pt_len + RK_TAG_LEN.
 * 4. Copy header into buffer, then plaintext after it.
 * 5. sg_init_one over the entire buffer.
 * 6. aead_request: set crypt with pt_len, set AD with RK_HDR_LEN.
 * 7. crypto_aead_encrypt. The header is AAD (authenticated, not encrypted).
 * 8. On success, copy the whole buffer to out, set *out_len, increment tx_seq.
 *
 * int rk_envelope_seal(struct rk_envelope_ctx *ctx,
 *                      const u8 *plaintext, size_t pt_len,
 *                      u8 *out, size_t *out_len)
 */

/*
 * TODO: Implement rk_envelope_open.
 *
 * Open and verify an envelope with replay protection.
 * 1. Validate env_len >= RK_HDR_LEN + RK_TAG_LEN.
 * 2. Parse the header from the first 18 bytes.
 * 3. Extract sequence from header. Reject if seq < rx_seq_expected (-EPROTO).
 * 4. Reconstruct the nonce from header fields.
 * 5. Copy envelope into a work buffer, sg_init_one.
 * 6. aead_request: set crypt with (env_len - RK_HDR_LEN), set AD.
 * 7. crypto_aead_decrypt. Returns -EBADMSG if header or payload tampered.
 * 8. On success, copy plaintext from buffer + RK_HDR_LEN to out.
 * 9. Update rx_seq_expected = seq + 1.
 *
 * int rk_envelope_open(struct rk_envelope_ctx *ctx,
 *                      const u8 *envelope, size_t env_len,
 *                      u8 *out, size_t *out_len)
 */

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

	sender.tfm = tfm;
	sender.session_id = session_id;
	sender.direction = RK_DIR_CLIENT;
	sender.tx_seq = 0;
	sender.rx_seq_expected = 0;

	receiver.tfm = tfm;
	receiver.session_id = session_id;
	receiver.direction = RK_DIR_CLIENT;
	receiver.tx_seq = 0;
	receiver.rx_seq_expected = 0;

	/*
	 * TODO: Self-test.
	 * 1. Seal "test" with rk_envelope_seal via sender ctx.
	 * 2. Open the envelope with rk_envelope_open via receiver ctx.
	 * 3. Verify opened_len == 4 and contents match "test".
	 * 4. Replay the same envelope. Expect -EPROTO.
	 * 5. If replay detected correctly, set error = 0; else error = -EIO.
	 */
	error = -ENOSYS;

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
MODULE_DESCRIPTION("Rklings exercise module");
