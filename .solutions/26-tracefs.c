// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>

static struct proc_dir_entry *proc_entry;
static struct dentry *dbg_dir;
static struct dentry *dbg_state;
static atomic_t access_count = ATOMIC_INIT(0);

static ssize_t tracer_show(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{
	char tmp[64];
	int len;

	atomic_inc(&access_count);
	len = snprintf(tmp, sizeof(tmp), "accesses=%d\n",
		       atomic_read(&access_count));
	return simple_read_from_buffer(buf, count, pos, tmp, len);
}

static const struct file_operations tracer_fops = {
	.read = tracer_show,
};

static ssize_t status_show(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{
	char tmp[128];
	int len;

	len = snprintf(tmp, sizeof(tmp),
		       "debugfs_dir=rk_tracer\nstate_file=state\naccesses=%d\n",
		       atomic_read(&access_count));
	return simple_read_from_buffer(buf, count, pos, tmp, len);
}

static const struct proc_ops status_ops = {
	.proc_read = status_show,
};

static int setup_debugfs(void)
{
	dbg_dir = debugfs_create_dir("rk_tracer", NULL);
	if (IS_ERR_OR_NULL(dbg_dir))
		return -ENOMEM;

	dbg_state = debugfs_create_file("state", 0444, dbg_dir,
					NULL, &tracer_fops);
	if (IS_ERR_OR_NULL(dbg_state)) {
		debugfs_remove(dbg_dir);
		return -ENOMEM;
	}
	return 0;
}

static int __init tracer_init(void)
{
	int err;

	err = setup_debugfs();
	if (err)
		return err;

	proc_entry = proc_create("rk_tracer", 0444, NULL, &status_ops);
	if (!proc_entry) {
		debugfs_remove_recursive(dbg_dir);
		return -ENOMEM;
	}

	pr_info("rk: debugfs entries at /sys/kernel/debug/rk_tracer/\n");
	return 0;
}

static void __exit tracer_exit(void)
{
	proc_remove(proc_entry);
	debugfs_remove_recursive(dbg_dir);
	pr_info("rk: debugfs entries removed\n");
}

module_init(tracer_init);
module_exit(tracer_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Debugfs interface for rootkit state");
