// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define SHA256_LEN 32
#define DATA_SIZE 256

static struct proc_dir_entry *entry;
static u8 digest[SHA256_LEN];
static char input_data[DATA_SIZE] = "rootkitlings verification data";
static int input_len;
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

static void rehash(void)
{
	hash_ok = (compute_sha256(input_data, input_len, digest) == 0);
}

static int hash_read(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "algo=sha256\nstatus=%s\nhash=",
		   hash_ok ? "ok" : "failed");
	if (hash_ok)
		for (i = 0; i < SHA256_LEN; i++)
			seq_printf(m, "%02x", digest[i]);
	seq_putc(m, '\n');
	return 0;
}

static int hash_open(struct inode *inode, struct file *file)
{
	return single_open(file, hash_read, NULL);
}

static ssize_t hash_write(struct file *file, const char __user *buffer,
			   size_t count, loff_t *position)
{
	size_t len = min(count, (size_t)(DATA_SIZE - 1));

	if (copy_from_user(input_data, buffer, len))
		return -EFAULT;

	if (len > 0 && input_data[len - 1] == '\n')
		len--;
	input_data[len] = '\0';
	input_len = len;
	rehash();

	return count;
}

static const struct proc_ops hash_proc_ops = {
	.proc_open    = hash_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = hash_write,
};

static int __init hashverify_init(void)
{
	input_len = strlen(input_data);
	rehash();

	entry = proc_create("rk_hash", 0666, NULL, &hash_proc_ops);
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
