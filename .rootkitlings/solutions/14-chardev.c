// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/seq_file.h>

static int device_show(struct seq_file *m, void *unused)
{
	seq_puts(m, "hello\n");
	return 0;
}

static int device_open(struct inode *inode, struct file *file)
{
	return single_open(file, device_show, NULL);
}

static const struct file_operations device_ops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct miscdevice device __maybe_unused = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rk_chardev",
	.fops = &device_ops,
};

static int __init device_init(void)
{
	return misc_register(&device);
}

static void __exit device_exit(void)
{
	misc_deregister(&device);
}

module_init(device_init);
module_exit(device_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Register a miscellaneous character device");
