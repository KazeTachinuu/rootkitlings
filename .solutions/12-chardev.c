// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/seq_file.h>

static int channel_show(struct seq_file *m, void *v)
{
	seq_puts(m, "hello\n");
	return 0;
}

static int channel_seq_open(struct inode *inode, struct file *file)
{
	pr_info("rk: opened by pid %d\n", current->pid);
	return single_open(file, channel_show, NULL);
}

static const struct file_operations channel_fops = {
	.owner   = THIS_MODULE,
	.open    = channel_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

static struct miscdevice channel_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rk_chardev",
	.fops  = &channel_fops,
};

static int __init dev_init(void)
{
	int err;

	err = misc_register(&channel_dev);
	if (err)
		return err;

	pr_info("rk: registered /dev/rk_chardev\n");
	return 0;
}

static void __exit dev_exit(void)
{
	misc_deregister(&channel_dev);
	pr_info("rk: deregistered /dev/rk_chardev\n");
}

module_init(dev_init);
module_exit(dev_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Character device communication via miscdevice");
