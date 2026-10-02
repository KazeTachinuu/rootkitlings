// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

#define CMD_SET_MSG _IOW('H', 1, char[64])
#define CMD_GET_MSG _IOR('H', 2, char[64])

static char command[64] = "hello";

static ssize_t command_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	char out[72];
	int len;

	len = snprintf(out, sizeof(out), "%s\n", command);
	return simple_read_from_buffer(buf, count, pos, out, len);
}

static ssize_t command_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	size_t len = count < sizeof(command) - 1 ? count : sizeof(command) - 1;

	if (copy_from_user(command, buf, len))
		return -EFAULT;
	if (len > 0 && command[len - 1] == '\n')
		len--;
	command[len] = '\0';
	pr_info("rk: message set to %s\n", command);
	return count;
}

static long command_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	char buf[64];

	switch (cmd) {
	case CMD_SET_MSG:
		if (copy_from_user(buf, (void __user *)arg, sizeof(buf)))
			return -EFAULT;
		buf[sizeof(buf) - 1] = '\0';
		memcpy(command, buf, sizeof(command));
		pr_info("rk: message set to %s\n", command);
		return 0;

	case CMD_GET_MSG:
		if (copy_to_user((void __user *)arg, command, sizeof(command)))
			return -EFAULT;
		return 0;

	default:
		return -ENOTTY;
	}
}

static const struct file_operations command_fops = {
	.owner          = THIS_MODULE,
	.read           = command_read,
	.write          = command_write,
	.unlocked_ioctl = command_ioctl,
};

static struct miscdevice command_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rk_ioctl",
	.fops  = &command_fops,
};

static int __init ctl_init(void)
{
	int err;

	err = misc_register(&command_dev);
	if (err)
		return err;

	pr_info("rk: ioctl interface ready\n");
	return 0;
}

static void __exit ctl_exit(void)
{
	misc_deregister(&command_dev);
	pr_info("rk: deregistered\n");
}

module_init(ctl_init);
module_exit(ctl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ioctl command dispatch");
