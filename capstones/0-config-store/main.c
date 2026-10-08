// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#include "config.h"

extern const struct proc_ops config_proc_ops;

static struct proc_dir_entry *config_entry;

static int __init config_init(void)
{
	config_entry = proc_create("rk_cfg", 0600, NULL, &config_proc_ops);
	return config_entry ? 0 : -ENOMEM;
}

static void __exit config_exit(void)
{
	proc_remove(config_entry);
	config_store_clear();
}

module_init(config_init);
module_exit(config_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Capstone 0: kernel config store with command execution");
