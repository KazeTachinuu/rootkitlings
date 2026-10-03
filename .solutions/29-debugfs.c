// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static struct proc_dir_entry *proc_entry;
static struct dentry *dbg_dir;
static struct dentry *dbg_state;
static atomic_t access_count = ATOMIC_INIT(0);

static int tracer_show(struct seq_file *m, void *v)
{
	atomic_inc(&access_count);
	seq_printf(m, "accesses=%d\n", atomic_read(&access_count));
	return 0;
}

static int tracer_open(struct inode *inode, struct file *file)
{
	return single_open(file, tracer_show, NULL);
}

static const struct file_operations tracer_fops = {
	.open    = tracer_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int status_show(struct seq_file *m, void *v)
{
	seq_printf(m, "debugfs_dir=rk_tracer\nstate_file=state\naccesses=%d\n",
		   atomic_read(&access_count));
	return 0;
}

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

	proc_entry = proc_create_single("rk_tracer", 0444, NULL, status_show);
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
MODULE_DESCRIPTION("Debugfs interface for module state");
