// SPDX-License-Identifier: GPL-2.0
#include <crypto/aead.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>

/*
 * 16-byte plaintext ---- AES-GCM encrypt ----> 16-byte ciphertext | 16-byte tag
 *                                                     |
 * 16-byte plaintext <--- verify + decrypt <-----------+
 *
 * The scatterlist uses kmalloc-backed storage. Module static data may live in
 * vmalloc space and must not be passed to sg_init_one().
 */

#define KEY_SIZE  16
#define IV_SIZE	  12
#define TEXT_SIZE 16
#define TAG_SIZE  16

static const u8 key[KEY_SIZE];
static const u8 iv[IV_SIZE];
static const u8 plaintext[TEXT_SIZE];
static const u8 expected[TEXT_SIZE + TAG_SIZE] = {
    0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92, 0xf3, 0x28, 0xc2,
    0xb9, 0x71, 0xb2, 0xfe, 0x78, 0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec,
    0x13, 0xbd, 0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf,
};

static struct proc_dir_entry *entry;
static u8 sealed[TEXT_SIZE + TAG_SIZE];
static u8 opened[TEXT_SIZE + TAG_SIZE];
static bool vector_ok;
static bool roundtrip_ok;
static bool tamper_rejected;

static int submit_request(bool encrypt, struct aead_request *req,
			  struct scatterlist *sg, unsigned int cryptlen,
			  u8 *request_iv)
{
	DECLARE_CRYPTO_WAIT(wait);

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  crypto_req_done, &wait);
	aead_request_set_ad(req, 0);
	aead_request_set_crypt(req, sg, sg, cryptlen, request_iv);
	return crypto_wait_req(encrypt ? crypto_aead_encrypt(req)
				       : crypto_aead_decrypt(req),
			       &wait);
}

static int run_aead(bool encrypt, u8 *buf)
{
	struct crypto_aead *tfm;
	struct aead_request *req;
	struct scatterlist sg;
	u8 request_iv[IV_SIZE];
	u8 *work;
	unsigned int cryptlen = encrypt ? TEXT_SIZE : TEXT_SIZE + TAG_SIZE;
	int err;

	tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);
	err = crypto_aead_setkey(tfm, key, sizeof(key));
	if (err)
		goto free_tfm;
	err = crypto_aead_setauthsize(tfm, TAG_SIZE);
	if (err)
		goto free_tfm;

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto free_tfm;
	}
	work = kmemdup(buf, TEXT_SIZE + TAG_SIZE, GFP_KERNEL);
	if (!work) {
		err = -ENOMEM;
		goto free_req;
	}
	memcpy(request_iv, iv, sizeof(request_iv));
	sg_init_one(&sg, work, TEXT_SIZE + TAG_SIZE);
	err = submit_request(encrypt, req, &sg, cryptlen, request_iv);
	if (!err)
		memcpy(buf, work, encrypt ? TEXT_SIZE + TAG_SIZE : TEXT_SIZE);
	memzero_explicit(work, TEXT_SIZE + TAG_SIZE);
	kfree(work);
free_req:
	aead_request_free(req);
free_tfm:
	crypto_free_aead(tfm);
	return err;
}

static int aead_show(struct seq_file *m, void *unused)
{
	int i;

	seq_puts(m, "algo=gcm(aes)\nsealed=");
	for (i = 0; i < sizeof(sealed); i++)
		seq_printf(m, "%02x", sealed[i]);
	seq_printf(m, "\nknown_answer=%s\nroundtrip=%s\ntamper_rejected=%s\n",
		   vector_ok ? "yes" : "no", roundtrip_ok ? "yes" : "no",
		   tamper_rejected ? "yes" : "no");
	return 0;
}

static int __init aead_init(void)
{
	u8 tampered[TEXT_SIZE + TAG_SIZE];
	int err;

	memcpy(sealed, plaintext, sizeof(plaintext));
	err = run_aead(true, sealed);
	if (err)
		return err;
	memcpy(opened, sealed, sizeof(sealed));
	err = run_aead(false, opened);
	if (err)
		return err;
	vector_ok = !memcmp(sealed, expected, sizeof(expected));
	roundtrip_ok = !memcmp(opened, plaintext, sizeof(plaintext));
	memcpy(tampered, sealed, sizeof(tampered));
	tampered[0] ^= 1;
	err = run_aead(false, tampered);
	tamper_rejected = err == -EBADMSG;
	memzero_explicit(tampered, sizeof(tampered));
	if (err && err != -EBADMSG)
		return err;
	entry = proc_create_single("rk_aead", 0400, NULL, aead_show);
	return entry ? 0 : -ENOMEM;
}

static void __exit aead_exit(void)
{
	proc_remove(entry);
	memzero_explicit(sealed, sizeof(sealed));
	memzero_explicit(opened, sizeof(opened));
}

module_init(aead_init);
module_exit(aead_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AES-GCM authenticated encryption known-answer test");
