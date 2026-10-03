// SPDX-License-Identifier: GPL-2.0
#include <crypto/skcipher.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#define AES_KEY_SIZE 16
#define DATA_SIZE 16

static struct proc_dir_entry *proc_entry;
static const u8 aes_key[AES_KEY_SIZE] = {
	0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
	0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
};
static const u8 aes_iv[AES_KEY_SIZE] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};
static const u8 plaintext[DATA_SIZE] = {
	0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
	0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
};
static const u8 expected_ciphertext[DATA_SIZE] = {
	0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
	0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
};
static u8 ciphertext[DATA_SIZE];
static u8 decrypted_buf[DATA_SIZE];
static bool crypto_ok;

static int do_aes(const u8 *in, u8 *out, const u8 *key, const u8 *iv,
		  bool encrypt)
{
	struct crypto_sync_skcipher *tfm;
	struct skcipher_request *req;
	struct scatterlist sg;
	u8 *buf;
	u8 request_iv[AES_KEY_SIZE];
	int err;

	tfm = crypto_alloc_sync_skcipher("cbc(aes)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	err = crypto_sync_skcipher_setkey(tfm, key, AES_KEY_SIZE);
	if (err)
		goto out_tfm;

	req = skcipher_request_alloc(&tfm->base, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto out_tfm;
	}

	buf = kmalloc(DATA_SIZE, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto out_req;
	}
	memcpy(buf, in, DATA_SIZE);

	memcpy(request_iv, iv, sizeof(request_iv));
	sg_init_one(&sg, buf, DATA_SIZE);
	skcipher_request_set_crypt(req, &sg, &sg, DATA_SIZE, request_iv);

	if (encrypt)
		err = crypto_skcipher_encrypt(req);
	else
		err = crypto_skcipher_decrypt(req);

	if (!err)
		memcpy(out, buf, DATA_SIZE);

	kfree(buf);
out_req:
	skcipher_request_free(req);
out_tfm:
	crypto_free_sync_skcipher(tfm);
	return err;
}

static int aes_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "algo=aes-cbc\nstatus=%s\nplain=",
		   crypto_ok ? "ok" : "failed");
	for (i = 0; i < DATA_SIZE; i++)
		seq_printf(m, "%02x", plaintext[i]);
	seq_puts(m, "\ncipher=");
	for (i = 0; i < DATA_SIZE; i++)
		seq_printf(m, "%02x", ciphertext[i]);
	seq_printf(m, "\nknown_answer=%s\nmatch=%s\n",
		   memcmp(ciphertext, expected_ciphertext, DATA_SIZE) == 0 ?
		   "yes" : "no",
		   memcmp(plaintext, decrypted_buf, DATA_SIZE) == 0 ?
		   "yes" : "no");
	return 0;
}

static int __init aes_demo_init(void)
{
	int err;

	err = do_aes(plaintext, ciphertext, aes_key, aes_iv, true);
	if (err) {
		pr_err("rk: AES encrypt failed: %d\n", err);
		return err;
	}

	err = do_aes(ciphertext, decrypted_buf, aes_key, aes_iv, false);
	if (err) {
		pr_err("rk: AES decrypt failed: %d\n", err);
		return err;
	}

	crypto_ok = !memcmp(ciphertext, expected_ciphertext, DATA_SIZE) &&
		    !memcmp(plaintext, decrypted_buf, DATA_SIZE);

	proc_entry = proc_create_single("rk_aes", 0444, NULL, aes_show);
	if (!proc_entry)
		return -ENOMEM;

	pr_info("rk: AES-CBC encryption demo, round-trip %s\n",
		crypto_ok ? "ok" : "FAILED");
	return 0;
}

static void __exit aes_demo_exit(void)
{
	proc_remove(proc_entry);
}

module_init(aes_demo_init);
module_exit(aes_demo_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AES-CBC symmetric encryption");
