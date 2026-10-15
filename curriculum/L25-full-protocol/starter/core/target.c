// SPDX-License-Identifier: GPL-2.0
// core/target.c - Spinlocked target lists for hiding.
#include <linux/string.h>
#include "rootkit.h"

struct rk_target_list rk_file_targets;
struct rk_target_list rk_pid_targets;
struct rk_target_list rk_content_targets;
struct rk_target_list rk_port_targets;

/*
 * TODO: Implement rk_target_init.
 *
 * spin_lock_init the list lock, set count to 0.
 */
void rk_target_init(struct rk_target_list *list)
{
}

/*
 * TODO: Implement rk_target_add.
 *
 * spin_lock_irqsave, check count < RK_TARGET_MAX,
 * strscpy into targets[count], increment count.
 * Return -ENOSPC if full.
 */
int rk_target_add(struct rk_target_list *list, const char *name)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_target_remove.
 *
 * spin_lock_irqsave, validate idx in range,
 * memmove remaining entries down, decrement count.
 * Return -EINVAL on bad index.
 */
int rk_target_remove(struct rk_target_list *list, int idx)
{
	return -ENOSYS;
}

/*
 * TODO: Implement rk_target_match.
 *
 * spin_lock_irqsave, walk targets, strcmp each.
 * Return true if name matches any entry.
 */
bool rk_target_match(struct rk_target_list *list, const char *name)
{
	return false;
}

/*
 * TODO: Implement rk_line_matches.
 *
 * spin_lock_irqsave, walk targets, strnstr each
 * against the line buffer.
 * Return true if any target appears in the line.
 */
bool rk_line_matches(struct rk_target_list *list,
		     const char *line, int len)
{
	return false;
}
