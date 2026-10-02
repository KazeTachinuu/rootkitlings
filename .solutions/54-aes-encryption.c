// SPDX-License-Identifier: GPL-2.0
#include <crypto/skcipher.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#define AES_KEY_SIZE 16
#define DATA_SIZE 32

static struct proc_dir_entry *proc_entry;
static u8 aes_key[AES_KEY_SIZE];
static u8 plaintext[DATA_SIZE] = "rootkit-encrypted-data!_padding";
static u8 ciphertext[DATA_SIZE];
static u8 decrypted_buf[DATA_SIZE];
static bool crypto_ok;

static int do_aes(const u8 *in, u8 *out, const u8 *key, bool encrypt)
{
	struct crypto_sync_skcipher *tfm;
	struct skcipher_request *req;
	struct scatterlist sg;
	u8 *buf;
	u8 iv[AES_KEY_SIZE];
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

	memset(iv, 0, sizeof(iv));
	sg_init_one(&sg, buf, DATA_SIZE);
	skcipher_request_set_crypt(req, &sg, &sg, DATA_SIZE, iv);

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

static ssize_t aes_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	char buf[256];
	int len, i, off;

	off = snprintf(buf, sizeof(buf), "algo=aes-cbc\nstatus=%s\nplain=",
		       crypto_ok ? "ok" : "failed");
	for (i = 0; i < DATA_SIZE; i++)
		off += snprintf(buf + off, sizeof(buf) - off, "%02x",
				plaintext[i]);
	off += snprintf(buf + off, sizeof(buf) - off, "\ncipher=");
	for (i = 0; i < DATA_SIZE; i++)
		off += snprintf(buf + off, sizeof(buf) - off, "%02x",
				ciphertext[i]);
	off += snprintf(buf + off, sizeof(buf) - off, "\nmatch=%s\n",
			memcmp(plaintext, decrypted_buf, DATA_SIZE) == 0 ?
			"yes" : "no");
	len = off;

	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops aes_proc_ops = {
	.proc_read = aes_read,
};

static int __init aes_demo_init(void)
{
	int err;

	get_random_bytes(aes_key, sizeof(aes_key));

	err = do_aes(plaintext, ciphertext, aes_key, true);
	if (err) {
		pr_err("rk: AES encrypt failed: %d\n", err);
		return err;
	}

	err = do_aes(ciphertext, decrypted_buf, aes_key, false);
	if (err) {
		pr_err("rk: AES decrypt failed: %d\n", err);
		return err;
	}

	crypto_ok = (memcmp(plaintext, decrypted_buf, DATA_SIZE) == 0);

	proc_entry = proc_create("rk_aes", 0444, NULL, &aes_proc_ops);
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
