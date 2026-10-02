// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/tracefs.h>

static struct dentry *trace_dir;
static struct dentry *trace_file;

/* TODO: tracefs file operations */

static int __init tracer_init(void)
{
	/* TODO: tracefs_create_dir + tracefs_create_file */
	return 0;
}

static void __exit tracer_exit(void)
{
	tracefs_remove(trace_dir);
}

module_init(tracer_init);
module_exit(tracer_exit);

MODULE_LICENSE("GPL");
