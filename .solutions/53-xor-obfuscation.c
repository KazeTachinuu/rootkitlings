// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#define XOR_KEY 0x42
#define BUF_SIZE 128

static struct proc_dir_entry *entry;
static char plaintext[BUF_SIZE] = "rootkit secret data";
static char ciphertext[BUF_SIZE];
static int data_len;

static void xor_encrypt(const char *in, char *out, int len, u8 key)
{
	int i;

	for (i = 0; i < len; i++)
		out[i] = in[i] ^ key;
}

static ssize_t obfusc_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	char buf[BUF_SIZE * 3 + 64];
	char decrypted[BUF_SIZE];
	int len, i, off;

	xor_encrypt(plaintext, ciphertext, data_len, XOR_KEY);
	xor_encrypt(ciphertext, decrypted, data_len, XOR_KEY);

	off = snprintf(buf, sizeof(buf), "plain=%s\ncipher=", plaintext);
	for (i = 0; i < data_len; i++)
		off += snprintf(buf + off, sizeof(buf) - off, "%02x",
				(u8)ciphertext[i]);
	off += snprintf(buf + off, sizeof(buf) - off, "\ndecrypt=%s\n",
			decrypted);
	len = off;

	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static ssize_t obfusc_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *position)
{
	size_t len = min(count, (size_t)(BUF_SIZE - 1));

	if (copy_from_user(plaintext, buffer, len))
		return -EFAULT;

	if (len > 0 && plaintext[len - 1] == '\n')
		len--;
	plaintext[len] = '\0';
	data_len = len;

	return count;
}

static const struct proc_ops obfusc_proc_ops = {
	.proc_read  = obfusc_read,
	.proc_write = obfusc_write,
};

static int __init xor_init(void)
{
	data_len = strlen(plaintext);

	entry = proc_create("rk_xor", 0666, NULL, &obfusc_proc_ops);
	if (!entry)
		return -ENOMEM;

	pr_info("rk: XOR obfuscation active (key=0x%02x)\n", XOR_KEY);
	return 0;
}

static void __exit xor_exit(void)
{
	proc_remove(entry);
}

module_init(xor_init);
module_exit(xor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("XOR string obfuscation");
