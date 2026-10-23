// SPDX-License-Identifier: GPL-2.0
#include "rklings_target.h"
#include <linux/errno.h>
#include <linux/string.h>

void rk_target_init(struct rk_target_list *list)
{
	spin_lock_init(&list->lock);
	list->count = 0;
}

int rk_target_add(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int i, ret = 0;

	if (!name || !name[0])
		return -EINVAL;
	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++)
		if (!strcmp(list->targets[i], name))
			goto out;
	if (list->count == RK_TARGET_MAX) {
		ret = -ENOSPC;
		goto out;
	}
	ret = strscpy(list->targets[list->count], name, RK_NAME_MAX);
	if (ret < 0)
		goto out;
	list->count++;
	ret = 0;
out:
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

int rk_target_remove(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int i, ret = -ENOENT;

	if (!name || !name[0])
		return -EINVAL;
	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->targets[i], name))
			continue;
		memmove(&list->targets[i], &list->targets[i + 1],
			(list->count - i - 1) * RK_NAME_MAX);
		list->count--;
		ret = 0;
		break;
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

bool rk_target_match(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++)
		if (!strcmp(list->targets[i], name)) {
			found = true;
			break;
		}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
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
