// SPDX-License-Identifier: GPL-2.0
#include <crypto/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define MESSAGE_MAX 128
#define TAG_SIZE    32

static const u8 shared_key[20] __maybe_unused = {[0 ... 19] = 0x0b};
static struct crypto_shash *tfm;
static struct proc_dir_entry *entry;
static DEFINE_MUTEX(message_lock);
static char message[MESSAGE_MAX] = "Hi There";
static size_t message_len = sizeof("Hi There") - 1;
static u8 tag[TAG_SIZE];

static int configure_hmac(void)
{
	int err;

	tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);
	err = crypto_shash_setkey(tfm, shared_key, sizeof(shared_key));
	if (err) {
		crypto_free_shash(tfm);
		tfm = NULL;
	}
	return err;
}

static int compute_tag(const void *data, size_t len, u8 *out)
{
	return crypto_shash_tfm_digest(tfm, data, len, out);
}

static int hmac_show(struct seq_file *m, void *unused)
{
	int i;

	mutex_lock(&message_lock);
	seq_printf(m, "message=%s\ntag=", message);
	for (i = 0; i < TAG_SIZE; i++)
		seq_printf(m, "%02x", tag[i]);
	seq_putc(m, '\n');
	mutex_unlock(&message_lock);
	return 0;
}

static int hmac_open(struct inode *inode, struct file *file)
{
	return single_open(file, hmac_show, NULL);
}

static ssize_t hmac_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *pos)
{
	char candidate[MESSAGE_MAX];
	u8 candidate_tag[TAG_SIZE];
	size_t len;
	int err;

	if (!count || count >= sizeof(candidate))
		return count ? -E2BIG : -EINVAL;
	if (copy_from_user(candidate, buf, count))
		return -EFAULT;
	len = count;
	if (candidate[len - 1] == '\n')
		len--;
	if (!len || memchr(candidate, '\0', len)) {
		memzero_explicit(candidate, sizeof(candidate));
		return -EINVAL;
	}
	candidate[len] = '\0';
	err = compute_tag(candidate, len, candidate_tag);
	if (err)
		goto clear;

	mutex_lock(&message_lock);
	memcpy(message, candidate, len + 1);
	message_len = len;
	memcpy(tag, candidate_tag, sizeof(tag));
	mutex_unlock(&message_lock);

clear:
	memzero_explicit(candidate, sizeof(candidate));
	memzero_explicit(candidate_tag, sizeof(candidate_tag));
	return err ? err : count;
}

static const struct proc_ops hmac_ops = {
	.proc_open = hmac_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = hmac_write,
};

static int __init hmac_init(void)
{
	int err;

	err = configure_hmac();
	if (err)
		return err;
	err = compute_tag(message, message_len, tag);
	if (err)
		goto free_tfm;
	entry = proc_create("rk_hmac", 0600, NULL, &hmac_ops);
	if (!entry) {
		err = -ENOMEM;
		goto free_tfm;
	}
	return 0;

free_tfm:
	crypto_free_shash(tfm);
	return err;
}

static void __exit hmac_exit(void)
{
	proc_remove(entry);
	crypto_free_shash(tfm);
	memzero_explicit(message, sizeof(message));
	memzero_explicit(tag, sizeof(tag));
}

module_init(hmac_init);
module_exit(hmac_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HMAC-SHA-256 command authentication");
