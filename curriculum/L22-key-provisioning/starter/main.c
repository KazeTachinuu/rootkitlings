// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64
#define RK_FP_LEN	4

/* TODO: Declare control_key as a module_param with type charp, permission 0 */

static u8 rk_key[RK_KEY_LEN];
static u8 rk_fingerprint[RK_FP_LEN];

/*
 * TODO: Implement rk_compute_fingerprint.
 *
 * Accepts a key buffer and its length, a fingerprint output buffer and its
 * length. Returns 0 on success, negative error on failure.
 *
 * 1. Allocate a "sha256" shash transform with crypto_alloc_shash.
 * 2. Allocate a shash_desc with kzalloc (sizeof(*desc) + descsize).
 * 3. Set desc->tfm, then call crypto_shash_digest to hash the key.
 * 4. Copy the first fp_len bytes of the digest into fp.
 * 5. Zeroize the digest, free the desc, free the transform.
 */

/*
 * TODO: Implement rk_key_verify.
 *
 * bool rk_key_verify(const u8 *incoming, size_t len);
 *
 * Return true if incoming is non-NULL, len == RK_FP_LEN, and the bytes
 * match rk_fingerprint. Return false otherwise.
 */

static int __init rk_init(void)
{
	/*
	 * TODO:
	 * 1. Reject load if control_key is NULL.
	 * 2. Reject load if strlen(control_key) != RK_KEY_HEX_LEN.
	 * 3. Parse hex into rk_key with hex2bin; reject on failure.
	 * 4. Zeroize the hex string with memzero_explicit.
	 * 5. Compute the fingerprint with rk_compute_fingerprint.
	 * 6. Build a u32 from the 4 fingerprint bytes (big-endian).
	 * 7. Print with pr_info("rklings: key loaded, fingerprint %08x\n", fp).
	 * 8. On any error after hex parsing, zeroize rk_key before returning.
	 */
	return -ENOSYS;
}

static void __exit rk_exit(void)
{
	/* TODO: Zeroize rk_key and rk_fingerprint with memzero_explicit */
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
