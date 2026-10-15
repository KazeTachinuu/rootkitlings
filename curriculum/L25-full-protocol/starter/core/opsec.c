// SPDX-License-Identifier: GPL-2.0
// core/opsec.c - Escalation, module cloaking, persistence.
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/module.h>
#include "rootkit.h"

/*
 * TODO: Implement rk_escalate.
 *
 * prepare_kernel_cred(&init_task) + commit_creds.
 * Return -ENOMEM if prepare_kernel_cred fails.
 */
int rk_escalate(void)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_cloak.
 *
 * Save THIS_MODULE->list.prev, then list_del(&THIS_MODULE->list).
 * Guard against double-cloaking.
 */
void rk_cloak(void)
{
}

/*
 * TODO: Implement rk_uncloak.
 *
 * list_add(&THIS_MODULE->list, saved_mod_list) to restore.
 * Only uncloak if currently cloaked.
 */
void rk_uncloak(void)
{
}

/*
 * TODO: Implement rk_persist.
 *
 * filp_open PERSIST_PATH with O_WRONLY | O_CREAT | O_TRUNC,
 * kernel_write MODULE_NAME_STR "\n", filp_close.
 */
int rk_persist(void)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_unpersist.
 *
 * filp_open PERSIST_PATH with O_WRONLY, filp_close.
 * (Truncating or removing the file disables auto-load.)
 */
int rk_unpersist(void)
{
	return -ENOSYS;
}
