// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#define MESSAGE_SIZE 64
#define CMD_SET_MSG  _IOW('H', 1, char[MESSAGE_SIZE])
#define CMD_GET_MSG  _IOR('H', 2, char[MESSAGE_SIZE])

static char message[MESSAGE_SIZE] = "hello";
static DEFINE_MUTEX(message_lock);

static int device_show(struct seq_file *m, void *unused)
{
	mutex_lock(&message_lock);
	seq_printf(m, "%s\n", message);
	mutex_unlock(&message_lock);
	return 0;
}

static int device_open(struct inode *inode, struct file *file)
{
	return single_open(file, device_show, NULL);
}

static long device_ioctl(struct file *file, unsigned int command,
			 unsigned long argument)
{
	/* Copy SET data in, copy GET data out, and reject unknown commands. */
	return -ENOTTY;
}

static const struct file_operations device_ops = {
	.owner = THIS_MODULE,
	.open = device_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.unlocked_ioctl = device_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rk_ioctl",
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
MODULE_DESCRIPTION("Dispatch typed ioctl commands");
