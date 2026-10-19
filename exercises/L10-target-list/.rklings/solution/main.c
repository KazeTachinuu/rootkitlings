// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/printk.h>
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

static int rk_target_add(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int ret;

	if (!name || !name[0])
		return -EINVAL;

	spin_lock_irqsave(&list->lock, flags);
	if (list->count >= RK_TARGET_MAX) {
		ret = -ENOSPC;
		goto out;
	}
	ret = strscpy(list->targets[list->count], name, RK_NAME_MAX);
	if (ret < 0)
		goto out;
	list->count++;
	ret = 0;
out:
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

static int rk_target_remove(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int i, ret = -ENOENT;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->targets[i], name) != 0)
			continue;
		if (i < list->count - 1)
			memmove(&list->targets[i], &list->targets[i + 1],
				(list->count - i - 1) * RK_NAME_MAX);
		list->count--;
		ret = 0;
		break;
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

static bool __maybe_unused rk_target_match(struct rk_target_list *list,
					   const char *name)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->targets[i], name) == 0) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}

static void rk_target_clear(struct rk_target_list *list)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	list->count = 0;
	spin_unlock_irqrestore(&list->lock, flags);
}

static int rk_proc_show(struct seq_file *m, void *v)
{
	char snapshot[RK_TARGET_MAX][RK_NAME_MAX];
	unsigned long flags;
	int count;
	int i;

	spin_lock_irqsave(&target_list.lock, flags);
	count = target_list.count;
	memcpy(snapshot, target_list.targets, count * RK_NAME_MAX);
	spin_unlock_irqrestore(&target_list.lock, flags);

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

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len] = '\0';

	// Strip trailing newline
	if (len > 0 && buf[len - 1] == '\n')
		buf[--len] = '\0';

	if (strncmp(buf, "add ", 4) == 0) {
		ret = rk_target_add(&target_list, buf + 4);
	} else if (strncmp(buf, "remove ", 7) == 0) {
		ret = rk_target_remove(&target_list, buf + 7);
	} else if (strcmp(buf, "clear") == 0) {
		rk_target_clear(&target_list);
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	if (ret)
		return ret;

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
	spin_lock_init(&target_list.lock);
	target_list.count = 0;

	proc_entry = proc_create("rklings", 0666, NULL, &rk_proc_ops);
	if (!proc_entry)
		return -ENOMEM;

	pr_info("rklings: loaded\n");
	return 0;
}

static void __exit rk_exit(void)
{
	proc_remove(proc_entry);
	pr_info("rklings: unloaded\n");
}

module_init(rk_init);
module_exit(rk_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rklings thread-safe target list");
