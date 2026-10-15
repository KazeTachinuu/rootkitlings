// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#define RK_KEY_LEN	32
#define RK_KEY_HEX_LEN	64
#define RK_FP_LEN	4

static char *control_key;
module_param(control_key, charp, 0);

static u8 rk_key[RK_KEY_LEN];
static u8 rk_fingerprint[RK_FP_LEN];

static int rk_compute_fingerprint(const u8 *key, size_t key_len,
				  u8 *fp, size_t fp_len)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	u8 digest[32];
	int error;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	desc->tfm = tfm;
	error = crypto_shash_digest(desc, key, key_len, digest);
	if (!error)
		memcpy(fp, digest, fp_len);

	memzero_explicit(digest, sizeof(digest));
	kfree(desc);
	crypto_free_shash(tfm);
	return error;
}

bool rk_key_verify(const u8 *incoming, size_t len)
{
	if (!incoming || len != RK_FP_LEN)
		return false;
	return memcmp(incoming, rk_fingerprint, RK_FP_LEN) == 0;
}

static int __init rk_init(void)
{
	u32 fp_val;
	int error;

	if (!control_key)
		return -EINVAL;
	if (strlen(control_key) != RK_KEY_HEX_LEN)
		return -EINVAL;
	if (hex2bin(rk_key, control_key, RK_KEY_LEN) < 0)
		return -EINVAL;

	/* Zeroize the hex string after parsing */
	memzero_explicit(control_key, RK_KEY_HEX_LEN);

	error = rk_compute_fingerprint(rk_key, RK_KEY_LEN,
				       rk_fingerprint, RK_FP_LEN);
	if (error)
		goto fail;

	fp_val = (rk_fingerprint[0] << 24) | (rk_fingerprint[1] << 16) |
		 (rk_fingerprint[2] << 8)  |  rk_fingerprint[3];
	pr_info("rklings: key loaded, fingerprint %08x\n", fp_val);
	return 0;

fail:
	memzero_explicit(rk_key, sizeof(rk_key));
	return error;
}

static void __exit rk_exit(void)
{
	memzero_explicit(rk_key, sizeof(rk_key));
	memzero_explicit(rk_fingerprint, sizeof(rk_fingerprint));
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
