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

/*
 * TODO: Implement rk_aead_seal.
 *
 * Encrypt and authenticate plaintext with AES-256-GCM.
 * Parameters:
 *   tfm       - allocated AEAD transform (gcm(aes))
 *   nonce     - 12-byte nonce
 *   aad       - additional authenticated data (may be NULL if aad_len == 0)
 *   aad_len   - length of AAD
 *   plaintext - data to encrypt
 *   pt_len    - length of plaintext
 *   out       - output buffer, must hold pt_len + RK_TAG_LEN bytes
 *   out_len   - set to pt_len + RK_TAG_LEN on success
 *
 * Steps:
 * 1. Allocate a buffer for aad_len + pt_len + RK_TAG_LEN.
 * 2. Copy AAD (if any) followed by plaintext into the buffer.
 * 3. sg_init_one covering the entire buffer.
 * 4. aead_request_alloc, set crypt (pt_len for encryption length), set AD.
 * 5. crypto_aead_encrypt. On success, copy ciphertext + tag from
 *    buffer + aad_len into out.
 * 6. Free the request and buffer.
 *
 * Returns 0 on success, negative error otherwise.
 */

/*
 * TODO: Implement rk_aead_open.
 *
 * Decrypt and verify ciphertext with AES-256-GCM.
 * Parameters:
 *   tfm        - allocated AEAD transform
 *   nonce      - 12-byte nonce (same one used to seal)
 *   aad        - additional authenticated data
 *   aad_len    - length of AAD
 *   ciphertext - ciphertext + tag (output of rk_aead_seal)
 *   ct_len     - length of ciphertext including 16-byte tag
 *   out        - output buffer for recovered plaintext
 *   out_len    - set to ct_len - RK_TAG_LEN on success
 *
 * Steps:
 * 1. Validate ct_len >= RK_TAG_LEN.
 * 2. Allocate buffer for aad_len + ct_len.
 * 3. Copy AAD (if any) followed by ciphertext into buffer.
 * 4. sg_init_one, aead_request_alloc, set crypt (ct_len), set AD.
 * 5. crypto_aead_decrypt. Returns -EBADMSG on authentication failure.
 * 6. On success, copy plaintext from buffer + aad_len into out.
 *
 * Returns 0 on success, -EBADMSG on tampered data.
 */

static int __init rk_init(void)
{
	struct crypto_aead *tfm;
	u8 key[RK_KEY_LEN];
	u8 nonce[RK_NONCE_LEN];
	u8 sealed[64];
	u8 opened[64];
	size_t sealed_len, opened_len;
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

	get_random_bytes(nonce, sizeof(nonce));

	/*
	 * TODO: Self-test.
	 * 1. Seal "hello" (5 bytes) with rk_aead_seal, no AAD.
	 * 2. Open the sealed ciphertext with rk_aead_open.
	 * 3. Verify opened_len == 5 and contents match "hello".
	 * 4. Flip sealed[0] ^= 0x01, try opening again.
	 * 5. Expect -EBADMSG. If so, set error = 0; otherwise error = -EIO.
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
