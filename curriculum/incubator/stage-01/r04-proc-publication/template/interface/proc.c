// SPDX-License-Identifier: GPL-2.0
#include <linux/seq_file.h>

#include "rootkit.h"

static int rk_status_show(struct seq_file *output, void *unused)
{
	seq_puts(output, "active\n");
	return 0;
}

/* TODO: own the /proc/rootkitlings entry here with rk_status_show(). */
