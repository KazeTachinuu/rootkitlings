// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/random.h>

static struct proc_dir_entry *entry;
static u32 session_id;
static u8 auth_token[16];

static int format_token(char *buf, size_t size)
{
	int len, i;

	len = snprintf(buf, size, "session=%08x\ntoken=", session_id);
	for (i = 0; i < 16; i++)
		len += snprintf(buf + len, size - len, "%02x", auth_token[i]);
	len += snprintf(buf + len, size - len, "\n");
	return len;
}

static ssize_t session_show(struct file *file, char __user *buffer,
			    size_t count, loff_t *position)
{
	char buf[128];
	int len;

	len = format_token(buf, sizeof(buf));
	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops session_proc_ops = {
	.proc_read = session_show,
};

static int __init session_init(void)
{
	session_id = get_random_u32();
	get_random_bytes(auth_token, sizeof(auth_token));

	pr_info("rk: session id=%08x\n", session_id);

	entry = proc_create("rk_entropy", 0444, NULL, &session_proc_ops);
	if (!entry)
		return -ENOMEM;

	return 0;
}

static void __exit session_exit(void)
{
	proc_remove(entry);
}

module_init(session_init);
module_exit(session_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel random number generation");
