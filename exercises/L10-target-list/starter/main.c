// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define RK_TARGET_MAX 32
#define RK_NAME_MAX   64

struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};

static struct rk_target_list target_list;
static struct proc_dir_entry *proc_entry;

// TODO: rk_target_add (strscpy into targets[count], return -ENOSPC if full)
// TODO: rk_target_remove (validate idx, memmove to close gap)
// TODO: rk_target_match (strcmp loop, return true on hit)
// TODO: rk_target_clear (reset count to 0)

static int rk_proc_show(struct seq_file *m, void *v)
{
	// TODO: print each target, one per line
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

static ssize_t rk_proc_write(struct file *file, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	// TODO: parse "add <name>", "remove <idx>", "clear" commands
	return count;
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
	// TODO: init spinlock, create /proc/rklings, pr_info loaded
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: remove proc entry, pr_info unloaded
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings thread-safe target list");
