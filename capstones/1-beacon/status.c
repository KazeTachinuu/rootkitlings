// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "beacon.h"
#include "status.h"

static struct proc_dir_entry *status_entry;

static int status_show(struct seq_file *output, void *unused)
{
	struct beacon_snapshot snapshot;

	beacon_snapshot(&snapshot);
	seq_printf(output,
		   "uptime: %lus\nticks: %lu\ninterval: %d\nstatus: running\n",
		   snapshot.uptime, snapshot.ticks, snapshot.interval);
	return 0;
}

int status_create(void)
{
	status_entry = proc_create_single("rk_beacon", 0444, NULL, status_show);
	return status_entry ? 0 : -ENOMEM;
}

void status_remove(void)
{
	proc_remove(status_entry);
}
