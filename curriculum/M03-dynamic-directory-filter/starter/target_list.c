// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include "target_list.h"

int rk_target_add(struct rk_target_list *list, const char *value)
{
	/*
	 * TODO: add a target to the list.
	 *
	 * Validate: value must be non-empty and shorter than
	 * RK_TARGET_MAX_LEN.
	 *
	 * Use spin_lock_irqsave / spin_unlock_irqrestore (NOT
	 * spin_lock) because match functions are called from
	 * kretprobe handlers which may run in interrupt context.
	 *
	 * Check list->count < RK_MAX_TARGETS, return -ENOSPC if full.
	 * Copy the string with strscpy, increment count.
	 * Return the index of the new entry.
	 */
	return -ENOSYS;
}

int rk_target_remove(struct rk_target_list *list, unsigned int index,
		     char *removed, size_t removed_size)
{
	/*
	 * TODO: remove a target by index.
	 *
	 * Validate index < count, return -EINVAL otherwise.
	 * Optionally copy the removed entry to `removed`.
	 * Compact the array by shifting later entries down.
	 * Decrement count.
	 */
	return -ENOSYS;
}

bool rk_target_match(struct rk_target_list *list, const char *name,
		     size_t name_length)
{
	/*
	 * TODO: return true if name contains any entry as a substring.
	 *
	 * Iterate under spin_lock_irqsave. For each entry, use
	 * strnstr(name, entry, name_length) to check for a match.
	 */
	return false;
}

bool rk_target_number_match(struct rk_target_list *list, unsigned int value)
{
	/*
	 * TODO: return true if value matches any entry parsed as uint.
	 *
	 * Iterate under spin_lock_irqsave. Parse each entry with
	 * kstrtouint. If it parses successfully and equals value,
	 * return true.
	 */
	return false;
}
