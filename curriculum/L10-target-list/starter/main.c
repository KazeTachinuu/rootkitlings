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

// TODO: Implement rk_target_add(list, name)
// Acquire the spinlock with spin_lock_irqsave.
// Check that count < RK_TARGET_MAX, return -ENOSPC if full.
// Copy name with strscpy, increment count.
// Release the spinlock with spin_unlock_irqrestore.

// TODO: Implement rk_target_remove(list, idx)
// Acquire the spinlock.
// Validate idx is in range [0, count).
// Use memmove to shift entries after idx down by one slot.
// Decrement count.
// Release the spinlock.

// TODO: Implement rk_target_match(list, name)
// Acquire the spinlock.
// Iterate over entries, strcmp each against name.
// Return true if found, false otherwise.
// Release the spinlock.

// TODO: Implement rk_target_clear(list)
// Acquire the spinlock, set count to 0, release.

// TODO: Implement rk_proc_show(m, v)
// Acquire the spinlock.
// Print each target with seq_printf, one per line.
// Release the spinlock.
static int rk_proc_show(struct seq_file *m, void *v)
{
	/* TODO: implement */
	return 0;
}

static int rk_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_proc_show, NULL);
}

// TODO: Implement rk_proc_write(file, ubuf, count, ppos)
// Copy user buffer (up to 127 bytes) with copy_from_user.
// Null-terminate and strip trailing newline.
// Parse "add <name>" -> call rk_target_add.
// Parse "remove <idx>" -> kstrtoint then rk_target_remove.
// Parse "clear" -> rk_target_clear.
// Return count.
static ssize_t rk_proc_write(struct file *file, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	/* TODO: implement */
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
	// TODO: Initialize the spinlock with spin_lock_init.
	// TODO: Set target_list.count to 0.
	// TODO: Create /proc/rklings with proc_create.
	// Return -ENOMEM if proc_create fails.
	return 0;
}

static void __exit rk_exit(void)
{
	// TODO: Remove the proc entry with proc_remove.
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings thread-safe target list");
