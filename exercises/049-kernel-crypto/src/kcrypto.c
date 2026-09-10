#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <crypto/skcipher.h>
#include <linux/scatterlist.h>

#define PROC_NAME "rootkitlings_crypto"
#define AES_BLOCK 16
#define KEY_SIZE  16

static struct proc_dir_entry *proc_entry;

static const u8 aes_key[KEY_SIZE] = "rootkitlingskey!";
static const u8 fixed_iv[AES_BLOCK] = "0123456789abcdef";

static int aes_crypt(const u8 *in, u8 *out, size_t len, const u8 *key,
		     const u8 *iv, bool encrypt)
{
	// TODO(rootkitlings): chiffre ou dechiffre avec AES-CBC.
	//
	// 1. Alloue le transform:
	//    struct crypto_skcipher *tfm = crypto_alloc_skcipher("cbc(aes)", 0, 0);
	//    Si IS_ERR(tfm), retourne l'erreur.
	//
	// 2. Definis la cle:
	//    crypto_skcipher_setkey(tfm, key, KEY_SIZE);
	//
	// 3. Alloue la requete:
	//    struct skcipher_request *req = skcipher_request_alloc(tfm, GFP_KERNEL);
	//
	// 4. Copie in dans out (on chiffre in-place dans out):
	//    memcpy(out, in, len);
	//
	// 5. Prepare le scatterlist:
	//    struct scatterlist sg;
	//    sg_init_one(&sg, out, len);
	//
	// 6. Copie l'IV dans un buffer local (il est modifie pendant le chiffrement):
	//    u8 iv_copy[AES_BLOCK];
	//    memcpy(iv_copy, iv, AES_BLOCK);
	//
	// 7. Configure la requete:
	//    skcipher_request_set_crypt(req, &sg, &sg, len, iv_copy);
	//
	// 8. Lance l'operation:
	//    if (encrypt) ret = crypto_skcipher_encrypt(req);
	//    else         ret = crypto_skcipher_decrypt(req);
	//
	// 9. Libere: skcipher_request_free(req); crypto_free_skcipher(tfm);
	//
	// 10 lignes de code (declarations + appels).

	return -1;
}

static ssize_t proc_write(struct file *file, const char __user *ubuf,
			   size_t count, loff_t *ppos)
{
	char plain[256];
	u8 encrypted[256];
	u8 decrypted[256];
	size_t len, padded_len;
	int ret, i;
	u8 pad_val;

	len = min(count, (size_t)240);
	if (copy_from_user(plain, ubuf, len))
		return -EFAULT;

	/* Strip newline */
	if (len > 0 && plain[len - 1] == '\n')
		len--;

	/* PKCS7 padding to AES block size */
	pad_val = AES_BLOCK - (len % AES_BLOCK);
	padded_len = len + pad_val;
	for (i = len; i < padded_len; i++)
		plain[i] = pad_val;

	pr_info("rootkitlings/049: plaintext (%zu bytes): %.*s\n", len, (int)len, plain);

	/* Encrypt */
	ret = aes_crypt((u8 *)plain, encrypted, padded_len, aes_key, fixed_iv, true);
	if (ret < 0) {
		pr_err("rootkitlings/049: encrypt failed: %d\n", ret);
		return count;
	}
	pr_info("rootkitlings/049: encrypted %zu bytes\n", padded_len);

	/* Decrypt */
	ret = aes_crypt(encrypted, decrypted, padded_len, aes_key, fixed_iv, false);
	if (ret < 0) {
		pr_err("rootkitlings/049: decrypt failed: %d\n", ret);
		return count;
	}

	/* Remove PKCS7 padding */
	pad_val = decrypted[padded_len - 1];
	if (pad_val <= AES_BLOCK)
		padded_len -= pad_val;
	decrypted[padded_len] = '\0';

	pr_info("rootkitlings/049: decrypted: %s\n", decrypted);

	if (memcmp(plain, decrypted, len) == 0)
		pr_info("rootkitlings/049: roundtrip OK\n");
	else
		pr_err("rootkitlings/049: roundtrip FAILED\n");

	return count;
}

static const struct proc_ops proc_fops = {
	.proc_write = proc_write,
};

static int __init kcrypto_init(void)
{
	proc_entry = proc_create(PROC_NAME, 0222, NULL, &proc_fops);
	if (!proc_entry)
		return -ENOMEM;
	pr_info("rootkitlings/049: /proc/%s created\n", PROC_NAME);
	return 0;
}

static void __exit kcrypto_exit(void)
{
	proc_remove(proc_entry);
	pr_info("rootkitlings/049: removed\n");
}

module_init(kcrypto_init);
module_exit(kcrypto_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rootkitlings 049: kernel crypto API (AES-CBC)");
