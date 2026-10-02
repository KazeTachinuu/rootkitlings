// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

struct board_state {
	char message[64];
	unsigned long reads;
};

static struct board_state *state;
static struct proc_dir_entry *entry;
static char *name = "world";

static int set_name(const char *value, const struct kernel_param *parameter)
{
	int error;

	error = param_set_charp(value, parameter);
	if (error)
		return error;

	snprintf(state->message, sizeof(state->message), "hello %s", name);
	pr_info("rk: name changed to %s\n", name);
	return 0;
}

static const struct kernel_param_ops name_ops = {
	.set = set_name,
	.get = param_get_charp,
};

module_param_cb(name, &name_ops, &name, 0644);
MODULE_PARM_DESC(name, "Name used by the greeting");

static ssize_t board_read(struct file *file, char __user *buffer,
			   size_t count, loff_t *position)
{
	char buf[96];
	int len;

	// Only count the first read of each open (position == 0)
	if (*position == 0)
		state->reads++;

	len = snprintf(buf, sizeof(buf), "%s (%lu reads)\n",
		       state->message, state->reads);

	return simple_read_from_buffer(buffer, count, position, buf, len);
}

static const struct proc_ops board_ops = {
	.proc_read = board_read,
};

static int __init board_init(void)
{
	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	snprintf(state->message, sizeof(state->message), "hello %s", name);
	state->reads = 0;

	entry = proc_create("rk_board", 0444, NULL, &board_ops);
	if (!entry) {
		kfree(state);
		state = NULL;
		return -ENOMEM;
	}

	pr_info("rk: message board ready\n");
	return 0;
}

static void __exit board_exit(void)
{
	// Reverse acquisition order: proc first, then heap
	proc_remove(entry);
	kfree(state);
	state = NULL;
	pr_info("rk: message board removed\n");
}

module_init(board_init);
module_exit(board_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 0: live message board");
