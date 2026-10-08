// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <crypto/utils.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define SHA256_LEN 32
#define DATA_SIZE  256

static struct proc_dir_entry *entry;
static u8 digest[SHA256_LEN];
static const u8 expected_digest[SHA256_LEN] = {
    0x5a, 0x3f, 0xaa, 0xe9, 0x79, 0x58, 0x48, 0x83, 0x5c, 0x76, 0x47,
    0xc5, 0x8a, 0x98, 0x43, 0xcf, 0x42, 0x48, 0x11, 0xd3, 0x77, 0x12,
    0xaa, 0x63, 0x40, 0xe4, 0x8e, 0x35, 0xc2, 0x8d, 0x6d, 0xec,
};
static char input_data[DATA_SIZE] = "rootkitlings verification data";
static size_t input_len;
static bool hash_ok;
static DEFINE_MUTEX(hash_lock);

static int compute_sha256(const void *data, unsigned int len, u8 *out)
{
	/* Allocate SHA-256, hash data into out, then release both objects. */
	return -EOPNOTSUPP;
}

static int rehash(void)
{
	int err = compute_sha256(input_data, input_len, digest);

	if (err)
		return err;
	hash_ok = !crypto_memneq(digest, expected_digest, sizeof(digest));
	return 0;
}

static int hash_read(struct seq_file *m, void *v)
{
	int i;

	mutex_lock(&hash_lock);
	seq_printf(m,
		   "algo=sha256\nstatus=%s\nhash=", hash_ok ? "ok" : "failed");
	for (i = 0; i < SHA256_LEN; i++)
		seq_printf(m, "%02x", digest[i]);
	seq_putc(m, '\n');
	mutex_unlock(&hash_lock);
	return 0;
}

static int hash_open(struct inode *inode, struct file *file)
{
	return single_open(file, hash_read, NULL);
}

static ssize_t hash_write(struct file *file, const char __user *buffer,
			  size_t count, loff_t *position)
{
	char input[DATA_SIZE];
	u8 candidate_digest[SHA256_LEN];
	size_t len = count;
	int err;

	if (!count)
		return -EINVAL;
	if (count >= sizeof(input))
		return -E2BIG;
	if (copy_from_user(input, buffer, len))
		return -EFAULT;

	if (len > 0 && input[len - 1] == '\n')
		len--;
	if (!len || memchr(input, '\0', len)) {
		memzero_explicit(input, sizeof(input));
		return -EINVAL;
	}
	input[len] = '\0';

	err = compute_sha256(input, len, candidate_digest);
	if (err)
		goto out;
	mutex_lock(&hash_lock);
	memcpy(input_data, input, len + 1);
	input_len = len;
	memcpy(digest, candidate_digest, sizeof(digest));
	hash_ok = !crypto_memneq(candidate_digest, expected_digest,
				    sizeof(candidate_digest));
	mutex_unlock(&hash_lock);
out:
	memzero_explicit(candidate_digest, sizeof(candidate_digest));
	memzero_explicit(input, sizeof(input));

	return err ? err : count;
}

static const struct proc_ops hash_proc_ops = {
	.proc_open = hash_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = hash_write,
};

static int __init hashverify_init(void)
{
	int err;

	input_len = strlen(input_data);
	err = rehash();
	if (err)
		return err;

	entry = proc_create("rk_hash", 0600, NULL, &hash_proc_ops);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit hashverify_exit(void)
{
	proc_remove(entry);
	memzero_explicit(input_data, sizeof(input_data));
	memzero_explicit(digest, sizeof(digest));
}

module_init(hashverify_init);
module_exit(hashverify_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-256 integrity verification");
