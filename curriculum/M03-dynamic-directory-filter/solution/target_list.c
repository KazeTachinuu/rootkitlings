// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "target_list.h"

int rk_target_add(struct rk_target_list *list, const char *value)
{
	unsigned long flags;
	size_t len;
	int index;

	if (!value || !value[0])
		return -EINVAL;
	len = strnlen(value, RK_TARGET_MAX_LEN);
	if (len >= RK_TARGET_MAX_LEN)
		return -EINVAL;

	spin_lock_irqsave(&list->lock, flags);
	if (list->count >= RK_MAX_TARGETS) {
		spin_unlock_irqrestore(&list->lock, flags);
		return -ENOSPC;
	}
	index = (int)list->count;
	strscpy(list->entries[index], value, RK_TARGET_MAX_LEN);
	list->count++;
	spin_unlock_irqrestore(&list->lock, flags);
	return index;
}

int rk_target_remove(struct rk_target_list *list, unsigned int index,
		     char *removed, size_t removed_size)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&list->lock, flags);
	if (index >= list->count) {
		spin_unlock_irqrestore(&list->lock, flags);
		return -EINVAL;
	}
	if (removed && removed_size > 0)
		strscpy(removed, list->entries[index], removed_size);
	for (i = index; i + 1 < list->count; i++)
		memcpy(list->entries[i], list->entries[i + 1], RK_TARGET_MAX_LEN);
	list->count--;
	spin_unlock_irqrestore(&list->lock, flags);
	return 0;
}

bool rk_target_match(struct rk_target_list *list, const char *name,
		     size_t name_length)
{
	unsigned long flags;
	unsigned int i;
	bool found = false;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		if (strnstr(name, list->entries[i], name_length)) {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}

bool rk_target_number_match(struct rk_target_list *list, unsigned int value)
{
	unsigned long flags;
	unsigned int i;
	bool found = false;

	spin_lock_irqsave(&list->lock, flags);
	for (i = 0; i < list->count; i++) {
		unsigned int entry_val;

		if (kstrtouint(list->entries[i], 10, &entry_val) == 0 &&
		    entry_val == value)  {
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return found;
}
