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

/* TODO: Implement the target list operations. All must be thread-safe
 * using spin_lock_irqsave/spin_unlock_irqrestore on list->lock.
 *   rk_target_add(list, name)    - append a name, return -ENOSPC if full
 *   rk_target_remove(list, idx)  - remove by index, shift remaining entries
 *   rk_target_match(list, name)  - return true if name is in the list
 *   rk_target_clear(list)        - reset count to 0 */

/* TODO: Implement rk_proc_show to print each target, one per line. */
static int rk_proc_show(struct seq_file *m, void *v)
{
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

/* TODO: Implement rk_proc_write to parse commands from userspace.
 * Accept "add <name>", "remove <idx>", and "clear". */
static ssize_t rk_proc_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	return count;
}

static const struct proc_ops rk_proc_ops = {
	.proc_open    = rk_proc_open,
	.proc_read    = seq_read,
	.proc_write   = rk_proc_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static int __init rk_init(void)
{
	/* TODO: Initialize the spinlock, create /proc/rklings (mode 0666). */
	return 0;
}

static void __exit rk_exit(void)
{
	/* TODO: Remove the proc entry. */
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings thread-safe target list");
