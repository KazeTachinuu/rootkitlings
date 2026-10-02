// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
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

static int board_read(struct seq_file *m, void *v)
{
	state->reads++;
	seq_printf(m, "%s (%lu reads)\n",
		   state->message, state->reads);
	return 0;
}


static int __init board_init(void)
{
	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	snprintf(state->message, sizeof(state->message), "hello %s", name);
	state->reads = 0;

	entry = proc_create_single("rk_board", 0444, NULL, board_read);
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
