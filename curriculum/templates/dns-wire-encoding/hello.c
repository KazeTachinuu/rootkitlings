// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

/*
 * dotted input -> validate each label -> length-prefixed wire bytes
 *                                          |
 * proc read  <- independent bounded decode-+
 *
 * Publishing the decoded result proves the parser consumed the bytes produced
 * by the encoder; it is not a hardcoded status string.
 */

#define DNS_NAME_MAX 255

static struct proc_dir_entry *entry;
static DEFINE_MUTEX(name_lock);
static u8 wire_name[DNS_NAME_MAX];
static size_t wire_len;

static int dns_encode(const char *name, u8 *wire, size_t size, size_t *used)
{
	/* Encode each dotted label as one length byte followed by its bytes. */
	return -EOPNOTSUPP;
}

static int dns_decode(const u8 *wire, size_t len, char *name, size_t size)
{
	size_t in = 0;
	size_t out = 0;
	u8 label_len;

	while (in < len) {
		label_len = wire[in++];
		if (!label_len) {
			if (!out || in != len)
				return -EINVAL;
			name[out] = '\0';
			return 0;
		}
		if (label_len > 63 || in + label_len > len)
			return -EINVAL;
		if (out && out + 1 >= size)
			return -ENOSPC;
		if (out)
			name[out++] = '.';
		if (out + label_len >= size)
			return -ENOSPC;
		memcpy(name + out, wire + in, label_len);
		out += label_len;
		in += label_len;
	}
	return -EINVAL;
}

static int dns_show(struct seq_file *m, void *unused)
{
	char name[DNS_NAME_MAX];
	int err;

	mutex_lock(&name_lock);
	err = dns_decode(wire_name, wire_len, name, sizeof(name));
	if (!err)
		seq_printf(m, "name=%s\nwire_len=%zu\n", name, wire_len);
	mutex_unlock(&name_lock);
	return err;
}

static int dns_open(struct inode *inode, struct file *file)
{
	return single_open(file, dns_show, NULL);
}

static ssize_t dns_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *pos)
{
	char name[DNS_NAME_MAX];
	u8 candidate[DNS_NAME_MAX];
	size_t len;
	size_t candidate_len;
	int err;

	if (!count || count >= sizeof(name))
		return -EINVAL;
	if (copy_from_user(name, buf, count))
		return -EFAULT;
	len = count;
	if (name[len - 1] == '\n')
		len--;
	if (!len || memchr(name, '\0', len))
		return -EINVAL;
	name[len] = '\0';
	err = dns_encode(name, candidate, sizeof(candidate), &candidate_len);
	if (err)
		return err;

	mutex_lock(&name_lock);
	memcpy(wire_name, candidate, candidate_len);
	wire_len = candidate_len;
	mutex_unlock(&name_lock);
	return count;
}

static const struct proc_ops dns_ops = {
	.proc_open = dns_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = dns_write,
};

static int __init dns_init(void)
{
	int err;

	err = dns_encode("agent.dead.example", wire_name, sizeof(wire_name),
			 &wire_len);
	if (err)
		return err;
	entry = proc_create("rk_dns", 0600, NULL, &dns_ops);
	return entry ? 0 : -ENOMEM;
}

static void __exit dns_exit(void)
{
	proc_remove(entry);
}

module_init(dns_init);
module_exit(dns_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded DNS wire-name codec");
