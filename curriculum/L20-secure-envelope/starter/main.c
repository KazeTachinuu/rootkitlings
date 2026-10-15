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

struct rk_envelope_hdr {
	u8 version;
	u8 direction;
	__be64 session_id;
	__be64 sequence;
} __packed;

struct rk_envelope_ctx {
	struct crypto_aead *tfm;
	u64 session_id;
	u8 direction;
	u64 tx_seq;
	u64 rx_seq_expected;
};

// Implement rk_build_nonce: 12-byte nonce from session_id, direction, sequence.

// Implement rk_envelope_seal: build header, encrypt with header as AAD.

// Implement rk_envelope_open: verify header, decrypt, check replay.

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

	// Self-test: seal "test", open it, verify. Replay same envelope, expect -EPROTO.
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
