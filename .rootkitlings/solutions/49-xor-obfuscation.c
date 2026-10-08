// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define XOR_KEY	    0x42
#define BUFFER_SIZE 128

static const u8 initial_ciphertext[] = {
    0x30, 0x2d, 0x2d, 0x36, 0x29, 0x2b, 0x36, 0x62, 0x31, 0x27,
    0x21, 0x30, 0x27, 0x36, 0x62, 0x26, 0x23, 0x36, 0x23,
};
static u8 ciphertext[BUFFER_SIZE];
static char plaintext[BUFFER_SIZE];
static size_t data_len;
static DEFINE_MUTEX(data_lock);
static struct proc_dir_entry *xor_entry;

static void xor_buf(const u8 *input, u8 *output, size_t len, u8 key)
{
	size_t i;

	for (i = 0; i < len; i++)
		output[i] = input[i] ^ key;
}

static void decode_ciphertext(void)
{
	xor_buf(ciphertext, (u8 *)plaintext, data_len, XOR_KEY);
	plaintext[data_len] = '\0';
}

static int xor_show(struct seq_file *m, void *unused)
{
	size_t i;

	mutex_lock(&data_lock);
	seq_puts(m, "cipher=");
	for (i = 0; i < data_len; i++)
		seq_printf(m, "%02x", ciphertext[i]);
	seq_printf(m, "\nplain=%s\n", plaintext);
	mutex_unlock(&data_lock);
	return 0;
}

static ssize_t xor_write(struct file *file, const char __user *buffer,
			 size_t count, loff_t *position)
{
	u8 input[BUFFER_SIZE];
	size_t len = count;

	if (!count)
		return -EINVAL;
	if (count >= sizeof(input))
		return -E2BIG;
	if (copy_from_user(input, buffer, len))
		return -EFAULT;
	if (len && input[len - 1] == '\n')
		len--;
	if (!len || memchr(input, '\0', len)) {
		memzero_explicit(input, sizeof(input));
		return -EINVAL;
	}

	mutex_lock(&data_lock);
	data_len = len;
	xor_buf(input, ciphertext, data_len, XOR_KEY);
	decode_ciphertext();
	mutex_unlock(&data_lock);

	memzero_explicit(input, sizeof(input));
	return count;
}

static int xor_open(struct inode *inode, struct file *file)
{
	return single_open(file, xor_show, NULL);
}

static const struct proc_ops xor_proc_ops = {
	.proc_open = xor_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = xor_write,
};

static int __init xor_init(void)
{
	data_len = ARRAY_SIZE(initial_ciphertext);
	memcpy(ciphertext, initial_ciphertext, data_len);
	decode_ciphertext();
	xor_entry = proc_create("rk_xor", 0600, NULL, &xor_proc_ops);
	return xor_entry ? 0 : -ENOMEM;
}

static void __exit xor_exit(void)
{
	proc_remove(xor_entry);
	memzero_explicit(plaintext, sizeof(plaintext));
}

module_init(xor_init);
module_exit(xor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("XOR string obfuscation");
