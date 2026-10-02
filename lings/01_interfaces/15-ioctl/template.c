// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

/* TODO: define ioctl command numbers with _IOW/_IOR */

static long ioctl_handler(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* TODO: dispatch on cmd */
	return -ENOTTY;
}

static const struct file_operations ioctl_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = ioctl_handler,
};

static struct miscdevice ioctl_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rk_ioctl",
	.fops  = &ioctl_fops,
};

static int __init ioctl_init(void)
{
	return misc_register(&ioctl_dev);
}

static void __exit ioctl_exit(void)
{
	misc_deregister(&ioctl_dev);
}

module_init(ioctl_init);
module_exit(ioctl_exit);

MODULE_LICENSE("GPL");
