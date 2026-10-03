// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/slab.h>

struct resource {
	int value;
};

static struct resource *primary;
static struct resource *secondary;
static bool fail_second;

module_param(fail_second, bool, 0444);

static int __init rollback_init(void)
{
	int err;

	primary = kmalloc_obj(*primary, GFP_KERNEL);
	if (!primary)
		return -ENOMEM;

	if (fail_second) {
		err = -EIO;
		goto free_primary;
	}

	secondary = kmalloc_obj(*secondary, GFP_KERNEL);
	if (!secondary) {
		err = -ENOMEM;
		goto free_primary;
	}

	pr_info("rk: both resources acquired\n");
	return 0;

free_primary:
	pr_info("rk: rolling back primary\n");
	kfree(primary);
	primary = NULL;
	return err;
}

static void __exit rollback_exit(void)
{
	kfree(secondary);
	secondary = NULL;
	kfree(primary);
	primary = NULL;
	pr_info("rk: resources released\n");
}

module_init(rollback_init);
module_exit(rollback_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Goto-chain rollback on init failure");
