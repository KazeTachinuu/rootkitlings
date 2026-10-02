// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>

struct implant_state {
	int value;
};

static struct implant_state *state;

static int __init alloc_init(void)
{
	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->value = 42;
	pr_info("rk: allocated value=%d\n", state->value);
	return 0;
}

static void __exit alloc_exit(void)
{
	kfree(state);
	state = NULL;
	pr_info("rk: freed state\n");
}

module_init(alloc_init);
module_exit(alloc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dynamic kernel memory allocation");
