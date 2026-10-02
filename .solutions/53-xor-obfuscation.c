// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
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

static int obfusc_read(struct seq_file *m, void *v)
{
	seq_printf(m, "plain=%s\ncipher=",
		   plaintext);
	return 0;
}

static int obfusc_open(struct inode *inode, struct file *file)
{
	return single_open(file, obfusc_read, NULL);
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
	.proc_open    = obfusc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
	.proc_write   = obfusc_write,
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
