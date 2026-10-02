// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/umh.h>

#define CMD_ADD   _IOW('H', 1, char[64])
#define CMD_CLEAR _IO('H', 2)
#define CMD_EXEC  _IO('H', 3)
#define OUTPUT_FILE "/tmp/.rk_cmd_out"

struct cmd_entry {
	char text[64];
	char output[256];
	int result;
	bool executed;
	struct list_head node;
};

static LIST_HEAD(cmd_list);
static struct proc_dir_entry *proc_entry;

static void clear_list(void)
{
	struct cmd_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, &cmd_list, node) {
		list_del(&e->node);
		kfree(e);
	}
}

static void exec_one(struct cmd_entry *e)
{
	char cmdline[128];
	char *argv[] = { "/bin/sh", "-c", cmdline, NULL };
	char *envp[] = { "PATH=/bin:/sbin:/usr/bin", NULL };
	struct file *f;
	loff_t pos = 0;
	int n;

	snprintf(cmdline, sizeof(cmdline), "%s > %s 2>&1", e->text, OUTPUT_FILE);
	e->result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	e->executed = true;

	f = filp_open(OUTPUT_FILE, O_RDONLY, 0);
	if (!IS_ERR(f)) {
		n = kernel_read(f, e->output, sizeof(e->output) - 1, &pos);
		filp_close(f, NULL);
		if (n > 0) {
			e->output[n] = '\0';
			if (n > 0 && e->output[n - 1] == '\n')
				e->output[n - 1] = '\0';
		} else {
			e->output[0] = '\0';
		}
	}
}

static void exec_all(void)
{
	struct cmd_entry *e;

	list_for_each_entry(e, &cmd_list, node) {
		if (!e->executed) {
			exec_one(e);
			pr_info("rk: exec '%s' -> %d\n", e->text, e->result);
		}
	}
}

static long box_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct cmd_entry *e;

	switch (cmd) {
	case CMD_ADD:
		e = kzalloc(sizeof(*e), GFP_KERNEL);
		if (!e)
			return -ENOMEM;
		if (copy_from_user(e->text, (void __user *)arg, 64)) {
			kfree(e);
			return -EFAULT;
		}
		e->text[63] = '\0';
		list_add_tail(&e->node, &cmd_list);
		return 0;
	case CMD_CLEAR:
		clear_list();
		return 0;
	case CMD_EXEC:
		exec_all();
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

	if (strncmp(tmp, "add:", 4) == 0) {
		struct cmd_entry *e = kzalloc(sizeof(*e), GFP_KERNEL);
		if (!e)
			return -ENOMEM;
		strscpy(e->text, tmp + 4, sizeof(e->text));
		list_add_tail(&e->node, &cmd_list);
	} else if (strcmp(tmp, "exec") == 0) {
		exec_all();
	} else if (strcmp(tmp, "clear") == 0) {
		clear_list();
	} else {
		return -EINVAL;
	}
	return count;
}

static ssize_t box_proc_read(struct file *f, char __user *buf,
			      size_t count, loff_t *pos)
{
	struct cmd_entry *e;
	char *page;
	int len = 0;
	ssize_t ret;

	page = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	list_for_each_entry(e, &cmd_list, node) {
		if (e->executed) {
			len += snprintf(page + len, PAGE_SIZE - len,
					"$ %s\n", e->text);
			if (e->output[0])
				len += snprintf(page + len, PAGE_SIZE - len,
						"%s\n", e->output);
		} else {
			len += snprintf(page + len, PAGE_SIZE - len,
					"[pending] %s\n", e->text);
		}
	}

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
	clear_list();
	pr_info("rk: command box removed\n");
}

module_init(cmdbox_init);
module_exit(cmdbox_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 1: command queue with execution and output");
