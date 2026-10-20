// SPDX-License-Identifier: GPL-2.0
#include "rklings_target.h"
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

static struct rk_target_list targets;
static struct proc_dir_entry *proc_entry;

static int rk_proc_show(struct seq_file *m, void *v)
{
	char snapshot[RK_TARGET_MAX][RK_NAME_MAX];
	int count = rk_target_snapshot(&targets, snapshot);
	int i;

	for (i = 0; i < count; i++)
		seq_printf(m, "%s\n", snapshot[i]);
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

static ssize_t rk_proc_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	char buf[128];
	size_t len;
	int ret;

	if (!count || count >= sizeof(buf))
		return -EINVAL;
	len = count;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len] = '\0';
	if (buf[len - 1] == '\n')
		buf[--len] = '\0';

	if (!strncmp(buf, "add ", 4))
		ret = rk_target_add(&targets, buf + 4);
	else if (!strncmp(buf, "remove ", 7))
		ret = rk_target_remove(&targets, buf + 7);
	else if (!strncmp(buf, "match ", 6))
		ret = rk_target_match(&targets, buf + 6) ? 0 : -ENOENT;
	else
		ret = -EINVAL;
	return ret ? ret : count;
}

static const struct proc_ops rk_proc_ops = {
	.proc_open = rk_proc_open,
	.proc_read = seq_read,
	.proc_write = rk_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init rk_init(void)
{
	rk_target_init(&targets);
	proc_entry = proc_create("rklings", 0600, NULL, &rk_proc_ops);
	return proc_entry ? 0 : -ENOMEM;
}

static void __exit rk_exit(void)
{
	proc_remove(proc_entry);
}

module_init(rk_init);
module_exit(rk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings concealment targets");
