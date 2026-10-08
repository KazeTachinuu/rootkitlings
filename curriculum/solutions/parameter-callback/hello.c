// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>

static char *name = "world";
static unsigned int changes;

static int name_param_set(const char *value, const struct kernel_param *kp)
{
	int err;

	err = param_set_charp(value, kp);
	if (err)
		return err;

	changes++;
	pr_info("rk: name changed to %s\n", name);
	return 0;
}

static const struct kernel_param_ops name_param_ops = {
	.set = name_param_set,
	.get = param_get_charp,
};

module_param_cb(name, &name_param_ops, &name, 0644);
MODULE_PARM_DESC(name, "Target identifier for the implant");
module_param(changes, uint, 0444);
MODULE_PARM_DESC(changes, "Number of successful name updates");

static int __init param_init(void)
{
	pr_info("rk: loaded name=%s\n", name);
	return 0;
}

static void __exit param_exit(void)
{
	pr_info("rk: unloaded name=%s\n", name);
}

module_init(param_init);
module_exit(param_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parameter change callback via kernel_param_ops");
