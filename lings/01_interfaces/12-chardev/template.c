// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

static ssize_t chardev_read(struct file *file, char __user *buffer,
			    size_t count, loff_t *position)
{
	/* TODO: return data to the reader */
	return 0;
}

static ssize_t chardev_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *position)
{
	/* TODO: store data from the writer */
	return count;
}

static const struct file_operations chardev_fops = {
	.owner = THIS_MODULE,
	.read  = chardev_read,
	.write = chardev_write,
};

static struct miscdevice chardev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rk_chardev",
	.fops  = &chardev_fops,
};

static int __init chardev_init(void)
{
	return misc_register(&chardev);
}

static void __exit chardev_exit(void)
{
	misc_deregister(&chardev);
}

module_init(chardev_init);
module_exit(chardev_exit);

MODULE_LICENSE("GPL");
