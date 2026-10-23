// SPDX-License-Identifier: GPL-2.0
#include "rklings_target.h"
#include <linux/errno.h>
#include <linux/string.h>

void rk_target_init(struct rk_target_list *list)
{
	spin_lock_init(&list->lock);
	list->count = 0;
}

// TODO 1: add one unique bounded name
int rk_target_add(struct rk_target_list *list, const char *name)
{
	return -ENOSYS;
}

// TODO 2: remove one exact name and close the gap
int rk_target_remove(struct rk_target_list *list, const char *name)
{
	return -ENOSYS;
}

// TODO 3: match one exact name
bool rk_target_match(struct rk_target_list *list, const char *name)
{
	return false;
}

int rk_target_snapshot(struct rk_target_list *list,
		       char snapshot[RK_TARGET_MAX][RK_NAME_MAX])
{
	unsigned long flags;
	int count;

	spin_lock_irqsave(&list->lock, flags);
	count = list->count;
	memcpy(snapshot, list->targets, count * RK_NAME_MAX);
	spin_unlock_irqrestore(&list->lock, flags);
	return count;
}
