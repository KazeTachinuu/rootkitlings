// SPDX-License-Identifier: GPL-2.0
#include "rootkit.h"
#include <linux/string.h>

struct rk_target_list rk_file_targets;
struct rk_target_list rk_pid_targets;
struct rk_target_list rk_content_targets;
struct rk_target_list rk_port_targets;

// TODO: rk_target_init (spin_lock_init, zero count)
void rk_target_init(struct rk_target_list *list)
{
}

// TODO: rk_target_add (spinlock, strscpy, bounds check)
int rk_target_add(struct rk_target_list *list, const char *name)
{
	return -ENOSYS;
}

// TODO: rk_target_remove (spinlock, exact match, memmove)
int rk_target_remove(struct rk_target_list *list, const char *name)
{
	return -ENOSYS;
}

// TODO: rk_target_unhide (remove the exact value)
int rk_target_unhide(struct rk_target_list *list, const char *arg)
{
	return -ENOSYS;
}

// TODO: rk_target_match (spinlock, strcmp loop)
bool rk_target_match(struct rk_target_list *list, const char *name)
{
	return false;
}

// TODO: rk_line_matches (spinlock, strnstr loop)
bool rk_line_matches(struct rk_target_list *list, const char *line, int len)
{
	return false;
}
