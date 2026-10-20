/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RKLINGS_TARGET_H
#define _RKLINGS_TARGET_H

#include <linux/spinlock.h>
#include <linux/types.h>

#define RK_TARGET_MAX 32
#define RK_NAME_MAX   64

struct rk_target_list {
	spinlock_t lock;
	char targets[RK_TARGET_MAX][RK_NAME_MAX];
	int count;
};

void rk_target_init(struct rk_target_list *list);
int rk_target_add(struct rk_target_list *list, const char *name);
int rk_target_remove(struct rk_target_list *list, const char *name);
bool rk_target_match(struct rk_target_list *list, const char *name);
int rk_target_snapshot(struct rk_target_list *list,
		       char snapshot[RK_TARGET_MAX][RK_NAME_MAX]);

#endif
