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

// Implement rk_aead_seal: encrypt and authenticate plaintext with AES-256-GCM.

// Implement rk_aead_open: decrypt and verify ciphertext with AES-256-GCM.

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

	// Self-test: seal "hello", open it, verify. Flip a byte, expect -EBADMSG.
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
