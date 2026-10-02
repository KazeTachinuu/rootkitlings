// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

#define SHA256_DIGEST_SIZE 32

static struct proc_dir_entry *entry;
static u8 digest[SHA256_DIGEST_SIZE];
static const char test_data[] = "rootkitlings verification data";
static bool hash_ok;

static int compute_sha256(const void *data, unsigned int len, u8 *out)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	int err;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
	if (!desc) {
		crypto_free_shash(tfm);
		return -ENOMEM;
	}

	desc->tfm = tfm;
	err = crypto_shash_digest(desc, data, len, out);

	kfree(desc);
	crypto_free_shash(tfm);
	return err;
}

static ssize_t hash_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	char buf[128];
	int len, i, off;

	off = snprintf(buf, sizeof(buf), "algo=sha256\nstatus=%s\nhash=",
		       hash_ok ? "ok" : "failed");
	for (i = 0; i < SHA256_DIGEST_SIZE; i++)
		off += snprintf(buf + off, sizeof(buf) - off, "%02x",
				digest[i]);
	off += snprintf(buf + off, sizeof(buf) - off, "\n");
	len = off;

	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops hash_proc_ops = {
	.proc_read = hash_read,
};

static int __init hashverify_init(void)
{
	int err;

	err = compute_sha256(test_data, sizeof(test_data) - 1, digest);
	hash_ok = (err == 0);

	entry = proc_create("rk_hash", 0444, NULL, &hash_proc_ops);
	if (!entry)
		return -ENOMEM;

	pr_info("rk: SHA-256 hash verification %s\n",
		hash_ok ? "ok" : "FAILED");
	return 0;
}

static void __exit hashverify_exit(void)
{
	proc_remove(entry);
}

module_init(hashverify_init);
module_exit(hashverify_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-256 integrity verification");
