// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "cmds.h"

static struct proc_dir_entry *proc_entry;

static long box_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	char text[64];

	switch (cmd) {
	case CMD_ADD:
		if (copy_from_user(text, (void __user *)arg, sizeof(text)))
			return -EFAULT;
		text[63] = '\0';
		return cmds_add(text);
	case CMD_CLEAR:
		cmds_clear();
		return 0;
	case CMD_EXEC:
		cmds_exec_all();
		return 0;
	default:
		return -ENOTTY;
	}
}

static ssize_t box_proc_write(struct file *f, const char __user *buf,
			       size_t count, loff_t *pos)
{
	char tmp[72];
	size_t len = count < sizeof(tmp) - 1 ? count : sizeof(tmp) - 1;

	if (copy_from_user(tmp, buf, len))
		return -EFAULT;
	tmp[len] = '\0';
	if (len > 0 && tmp[len - 1] == '\n')
		tmp[--len] = '\0';

	if (strncmp(tmp, "add:", 4) == 0)
		return cmds_add(tmp + 4) ? : count;
	if (strcmp(tmp, "exec") == 0) {
		cmds_exec_all();
		return count;
	}
	if (strcmp(tmp, "clear") == 0) {
		cmds_clear();
		return count;
	}
	return -EINVAL;
}

static ssize_t box_proc_read(struct file *f, char __user *buf,
			      size_t count, loff_t *pos)
{
	char *page;
	int len;
	ssize_t ret;

	page = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	len = cmds_format(page, PAGE_SIZE);
	ret = simple_read_from_buffer(buf, count, pos, page, len);
	kfree(page);
	return ret;
}

static const struct file_operations box_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = box_ioctl,
};

static struct miscdevice box_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rk_cmdbox",
	.fops  = &box_fops,
};

static const struct proc_ops box_proc_ops = {
	.proc_read  = box_proc_read,
	.proc_write = box_proc_write,
};

static int __init cmdbox_init(void)
{
	int err;

	err = misc_register(&box_dev);
	if (err)
		return err;

	proc_entry = proc_create("rk_cmdbox", 0666, NULL, &box_proc_ops);
	if (!proc_entry) {
		misc_deregister(&box_dev);
		return -ENOMEM;
	}

	pr_info("rk: command box ready\n");
	return 0;
}

static void __exit cmdbox_exit(void)
{
	proc_remove(proc_entry);
	misc_deregister(&box_dev);
	cmds_clear();
	pr_info("rk: command box removed\n");
}

module_init(cmdbox_init);
module_exit(cmdbox_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 1: command queue with execution and output");
